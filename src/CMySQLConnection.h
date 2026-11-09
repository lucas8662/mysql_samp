#pragma once
#ifndef INC_CMYSQLCONNECTION_H
#define INC_CMYSQLCONNECTION_H


#include <string>
#include <boost/atomic.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/chrono.hpp>
#include <queue>

using std::string;
using boost::atomic;
using boost::thread;
using boost::function;
using std::queue;

#ifdef WIN32
	#include <WinSock2.h>
	#include <mysql.h>
#else
	#include <mysql.h>
#endif


class CMySQLQuery;

struct CMySQLTLSOptions
{
	CMySQLTLSOptions() : Enabled(false) {}

	bool Enabled;
	string KeyFile;
	string CertFile;
	string CAFile;
	string CAPath;
	string Cipher;
};

class CMySQLConnection
{
public:
	static CMySQLConnection *Create(string &host, string &user, string &passwd, string &db, size_t port, bool auto_reconnect, bool threaded = true, const CMySQLTLSOptions& tls = CMySQLTLSOptions());
	void Destroy();

	//(dis)connect to the MySQL server
	bool Connect();
	bool Disconnect();
	//close and reopen the connection as one step (queued as a single job when threaded)
	bool Reconnect();

	//only meaningful for unthreaded connections or from inside the query thread
	inline bool IsConnected() const
	{
		return m_IsConnected;
	}

	//escape a string to dest
	bool EscapeString(const char *src, string &dest);

	//set character set
	bool SetCharset(string charset);

	inline MYSQL *GetMysqlPtr()
	{
		return m_Connection;
	}

	inline bool QueueQuery(CMySQLQuery *query)
	{
		if (!m_QueryQueue.push(query))
			return false;
		WakeUp();
		return true;
	}

	inline bool operator==(CMySQLConnection &rhs)
	{
		return (rhs.m_Port == m_Port && rhs.m_Host.compare(m_Host) == 0 && rhs.m_User.compare(m_User) == 0 && rhs.m_Database.compare(m_Database) == 0 && rhs.m_Passw.compare(m_Passw) == 0
			&& rhs.m_TLS.Enabled == m_TLS.Enabled && rhs.m_TLS.KeyFile == m_TLS.KeyFile && rhs.m_TLS.CertFile == m_TLS.CertFile
			&& rhs.m_TLS.CAFile == m_TLS.CAFile && rhs.m_TLS.CAPath == m_TLS.CAPath && rhs.m_TLS.Cipher == m_TLS.Cipher);
	}

private: //functions
	void ProcessQueries();
	void QueueFunction(const function<bool()> &func);
	void WakeUp();
	bool HasPendingFunctions();
	bool HasPendingWork();
	void ScheduleReconnect(bool failed);

private: //variables
	CMySQLConnection(string &host, string &user, string &passw, string &db, size_t port, bool auto_reconnect, bool threaded, const CMySQLTLSOptions& tls);
	~CMySQLConnection();


	thread *m_QueryThread;
	atomic<bool> m_QueryThreadRunning;

	boost::lockfree::spsc_queue<
		CMySQLQuery *,
		boost::lockfree::fixed_sized<true>,
		boost::lockfree::capacity<16876>
	> m_QueryQueue;

	boost::mutex m_FuncQueueMtx;
	queue<function<bool()> > m_FuncQueue;

	//wakes the query thread as soon as work arrives instead of polling
	boost::mutex m_WakeMtx;
	boost::condition_variable m_WakeCond;

	//background reconnect state; only touched by the query thread
	bool m_ConnectRequested;
	unsigned int m_ReconnectDelayMs;
	boost::chrono::steady_clock::time_point m_NextReconnect;


	//MySQL server login values
	string
		m_Host,
		m_User,
		m_Passw,
		m_Database;
	size_t m_Port;

	//connection status
	bool m_IsConnected;

	//automatic reconnect
	bool m_AutoReconnect;
	CMySQLTLSOptions m_TLS;

	//internal MYSQL pointer
	MYSQL *m_Connection;
};


#endif // INC_CMYSQLCONNECTION_H
