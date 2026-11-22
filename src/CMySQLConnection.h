#pragma once
#ifndef INC_CMYSQLCONNECTION_H
#define INC_CMYSQLCONNECTION_H


#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

using std::string;

#ifdef WIN32
	#include <WinSock2.h>
	#include <mysql.h>
#else
	#include <mysql.h>
#endif


class CMySQLQuery;
class CQueryQueue;

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
	// With a queue the connection gets its own thread and runs the queries
	// taken from it; without one it is unthreaded (the main connection).
	// The queue must outlive the connection. name identifies it in the log,
	// e.g. "1/tquery" or "1/pool#2".
	static CMySQLConnection *Create(string &host, string &user, string &passwd, string &db, size_t port, bool auto_reconnect, CQueryQueue *queue = nullptr, const CMySQLTLSOptions& tls = CMySQLTLSOptions(), const string &name = "main");
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

	inline bool operator==(CMySQLConnection &rhs)
	{
		return (rhs.m_Port == m_Port && rhs.m_Host.compare(m_Host) == 0 && rhs.m_User.compare(m_User) == 0 && rhs.m_Database.compare(m_Database) == 0 && rhs.m_Passw.compare(m_Passw) == 0
			&& rhs.m_TLS.Enabled == m_TLS.Enabled && rhs.m_TLS.KeyFile == m_TLS.KeyFile && rhs.m_TLS.CertFile == m_TLS.CertFile
			&& rhs.m_TLS.CAFile == m_TLS.CAFile && rhs.m_TLS.CAPath == m_TLS.CAPath && rhs.m_TLS.Cipher == m_TLS.Cipher);
	}

	friend struct TestAccess; //tests/TestAccess.h
private: //functions
	void ProcessQueries();
	void QueueFunction(std::function<bool()> func);
	//true when called from another thread than this connection's query thread
	bool MustQueue() const
	{
		return m_QueryThread.joinable() && std::this_thread::get_id() != m_QueryThread.get_id();
	}
	void WakeUp();
	bool HasPendingFunctions();
	//called with the query queue locked
	bool HasPendingWork(bool has_queries);
	void ScheduleReconnect(bool failed);
	//"CMySQLConnection::<function>[<name>]" for the log's function column
	string LogName(const char *function) const
	{
		return string("CMySQLConnection::") + function + "[" + m_Name + "]";
	}

private: //variables
	CMySQLConnection(string &host, string &user, string &passw, string &db, size_t port, bool auto_reconnect, CQueryQueue *queue, const CMySQLTLSOptions& tls, const string &name);
	~CMySQLConnection();


	std::thread m_QueryThread; //not joinable for unthreaded connections
	std::atomic<bool> m_QueryThreadRunning;

	//queries this connection's thread runs; owned by the handle, possibly
	//shared with other connections (pool). NULL for unthreaded connections.
	CQueryQueue *m_Queue;
	const string m_Name; //for the log only

	std::mutex m_FuncQueueMtx;
	std::queue<std::function<bool()> > m_FuncQueue;

	//background reconnect state; only touched by the query thread
	bool m_ConnectRequested;
	unsigned int m_ReconnectDelayMs;
	std::chrono::steady_clock::time_point m_NextReconnect;


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
