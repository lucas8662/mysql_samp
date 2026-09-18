#pragma once
#ifndef INC_CMYSQLCONNECTION_H
#define INC_CMYSQLCONNECTION_H


#include <string>
#include <boost/atomic.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
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

	//escape a string to dest
	bool EscapeString(const char *src, string &dest);

	//set character set
	bool SetCharset(string charset);

	inline MYSQL *GetMysqlPtr()
	{
		return m_Connection;
	}

	inline void QueueQuery(CMySQLQuery *query)
	{
		m_QueryQueue.push(query);
	}

	inline bool operator==(CMySQLConnection &rhs)
	{
		return (rhs.m_Host.compare(m_Host) == 0 && rhs.m_User.compare(m_User) == 0 && rhs.m_Database.compare(m_Database) == 0 && rhs.m_Passw.compare(m_Passw) == 0
			&& rhs.m_TLS.Enabled == m_TLS.Enabled && rhs.m_TLS.KeyFile == m_TLS.KeyFile && rhs.m_TLS.CertFile == m_TLS.CertFile
			&& rhs.m_TLS.CAFile == m_TLS.CAFile && rhs.m_TLS.CAPath == m_TLS.CAPath && rhs.m_TLS.Cipher == m_TLS.Cipher);
	}

private: //functions
	void ProcessQueries();

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
