#include "CMySQLConnection.h"
#include "CMySQLQuery.h"
#include "CMySQLResult.h"
#include "CCallback.h"
#include "CLog.h"

#include <boost/chrono.hpp>
namespace chrono = boost::chrono;
namespace this_thread = boost::this_thread;

// Connector/C client error codes for a dead connection (errmsg.h).
static const unsigned int ERROR_SERVER_GONE = 2006; // CR_SERVER_GONE_ERROR
static const unsigned int ERROR_SERVER_LOST = 2013; // CR_SERVER_LOST

// Background reconnect backoff for threaded connections.
static const unsigned int RECONNECT_DELAY_MIN_MS = 1000;
static const unsigned int RECONNECT_DELAY_MAX_MS = 30000;
// Upper bound for a single connection attempt, so a dead host does not block
// the query thread (or the server thread for the main connection) for minutes.
static const unsigned int CONNECT_TIMEOUT_SECONDS = 10;
// Safety net only: producers wake the thread explicitly.
static const unsigned int IDLE_WAIT_MS = 500;


CMySQLConnection *CMySQLConnection::Create(string &host, string &user, string &passwd, string &db, unsigned int port, bool auto_reconnect, bool threaded /*= true*/, const CMySQLTLSOptions& tls)
{
	return new CMySQLConnection(host, user, passwd, db, port, auto_reconnect, threaded, tls);
}

void CMySQLConnection::Destroy()
{
	delete this;
}

CMySQLConnection::CMySQLConnection(string &host, string &user, string &passw, string &db, size_t port, bool auto_reconnect, bool threaded, const CMySQLTLSOptions& tls)
	: 
	m_QueryThread(NULL),
	m_QueryThreadRunning(true),

	m_ConnectRequested(false),
	m_ReconnectDelayMs(RECONNECT_DELAY_MIN_MS),

	m_Host(host),
	m_User(user),
	m_Passw(passw),
	m_Database(db),
	m_Port(port),

	m_IsConnected(false),
	m_AutoReconnect(auto_reconnect),
	m_TLS(tls),

	m_Connection(NULL)
{
	if(threaded)
		m_QueryThread = new thread(boost::bind(&CMySQLConnection::ProcessQueries, this));
}

CMySQLConnection::~CMySQLConnection()
{
	if(m_QueryThread != NULL)
	{
		// Do not leave queued work running against a connection that is about to
		// disappear.  The old path queued Disconnect(), then destroyed this
		// object immediately, which left the worker with a dangling pointer.
		m_QueryThreadRunning = false;
		WakeUp();
		m_QueryThread->join();
		delete m_QueryThread;
		m_QueryThread = NULL;
	
		CMySQLQuery *query = NULL;
		while(m_QueryQueue.pop(query))
			delete query;
	}

	if (m_Connection != NULL)
	{
		mysql_close(m_Connection);
		m_Connection = NULL;
		m_IsConnected = false;
	}
}

bool CMySQLConnection::Connect()
{
	if(m_QueryThread != NULL && this_thread::get_id() != m_QueryThread->get_id()) //not in query thread and threaded: queue
	{
		QueueFunction(boost::bind(&CMySQLConnection::Connect, this));
	}
	else //in query thread or unthreaded: execute
	{
		CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLConnection::Connect", "establishing connection to database...");

		if (m_Connection == NULL)
		{
			m_Connection = mysql_init(NULL);
			if (m_Connection == NULL)
				CLog::Get()->LogFunction(LOG_ERROR, "CMySQLConnection::Connect", "MySQL initialization failed");
		}
		if (m_Connection == NULL)
			return false;

		m_ConnectRequested = true;

		// Options are reset by the connector after a failed attempt, so they
		// are applied again on every try.
		unsigned int connectTimeout = CONNECT_TIMEOUT_SECONDS;
		mysql_options(m_Connection, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout);

		// Connector/C can retain SSL options from its defaults.  R39's normal
		// mysql_connect must remain a plain connection; TLS is only mandatory
		// when the caller opted in through mysql_connect_ssl or mysql.ini.
		my_bool requireTls = m_TLS.Enabled ? 1 : 0;
		my_bool verifyTlsCertificate = m_TLS.Enabled ? 1 : 0;
		mysql_options(m_Connection, MYSQL_OPT_SSL_ENFORCE, &requireTls);
		mysql_options(m_Connection, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verifyTlsCertificate);

		if (!m_IsConnected && m_TLS.Enabled)
		{
			const int tlsResult = mysql_ssl_set(
				m_Connection,
				m_TLS.KeyFile.empty() ? NULL : m_TLS.KeyFile.c_str(),
				m_TLS.CertFile.empty() ? NULL : m_TLS.CertFile.c_str(),
				m_TLS.CAFile.empty() ? NULL : m_TLS.CAFile.c_str(),
				m_TLS.CAPath.empty() ? NULL : m_TLS.CAPath.c_str(),
				m_TLS.Cipher.empty() ? NULL : m_TLS.Cipher.c_str());
			if (tlsResult != 0)
			{
				CLog::Get()->LogFunction(LOG_ERROR, "CMySQLConnection::Connect", "unable to configure TLS: %s", mysql_error(m_Connection));
				return false;
			}
		}

		if (!m_IsConnected && !mysql_real_connect(m_Connection, m_Host.c_str(), m_User.c_str(), m_Passw.c_str(), m_Database.c_str(), m_Port, NULL, CLIENT_MULTI_RESULTS | (m_TLS.Enabled ? CLIENT_SSL : 0)))
		{
			CLog::Get()->LogFunction(LOG_ERROR, "CMySQLConnection::Connect", "(error #%d) %s", mysql_errno(m_Connection), mysql_error(m_Connection));

			m_IsConnected = false;
			ScheduleReconnect(true);
		}
		else
		{
			CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLConnection::Connect", "connection was successful");

			my_bool reconnect = m_AutoReconnect;
			mysql_options(m_Connection, MYSQL_OPT_RECONNECT, &reconnect);
			CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLConnection::Connect", "auto-reconnect has been %s", m_AutoReconnect == true ? "enabled" : "disabled");

			m_IsConnected = true;
			ScheduleReconnect(false);
		}
	}
	return true;
}

bool CMySQLConnection::Disconnect()
{
	if(m_QueryThread != NULL && this_thread::get_id() != m_QueryThread->get_id()) //not in query thread and threaded: queue
	{
		QueueFunction(boost::bind(&CMySQLConnection::Disconnect, this));
	}
	else //in query thread or unthreaded: execute
	{
		// An explicit disconnect stops the background reconnect attempts.
		m_ConnectRequested = false;

		if (m_Connection == NULL || m_IsConnected == false)
			CLog::Get()->LogFunction(LOG_WARNING, "CMySQLConnection::Disconnect", "no connection available");
		else
		{
			m_IsConnected = false;
			mysql_close(m_Connection);
			m_Connection = NULL;
			CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLConnection::Disconnect", "connection was closed");
		}
	}
	return true;
}

bool CMySQLConnection::Reconnect()
{
	if(m_QueryThread != NULL && this_thread::get_id() != m_QueryThread->get_id()) //not in query thread and threaded: queue
	{
		// Queued as one job: queuing Disconnect and Connect separately allowed a
		// deferred Disconnect to run after Connect and leave the link closed.
		QueueFunction(boost::bind(&CMySQLConnection::Reconnect, this));
	}
	else //in query thread or unthreaded: execute
	{
		if (m_IsConnected)
			Disconnect();
		Connect();
	}
	return true;
}

bool CMySQLConnection::EscapeString(const char *src, string &dest)
{
	dest.clear();
	if (src != NULL && m_IsConnected && m_Connection != NULL)
	{
		const size_t src_len = strlen(src);
		char *tmpEscapedStr = static_cast<char *>(malloc((src_len * 2 + 1) * sizeof(char)));
		if (tmpEscapedStr == NULL) return false;

		const unsigned long length = mysql_real_escape_string(m_Connection, tmpEscapedStr, src, src_len);
		if (length > src_len * 2) { free(tmpEscapedStr); return false; }
		dest.assign(tmpEscapedStr, length);

		free(tmpEscapedStr);
		return true;
	}
	return false;
}

bool CMySQLConnection::SetCharset(string charset)
{
	if(m_QueryThread != NULL && this_thread::get_id() != m_QueryThread->get_id()) //not in query thread and threaded: queue
	{
		QueueFunction(boost::bind(&CMySQLConnection::SetCharset, this, charset));
	}
	else //in query thread or unthreaded: execute
	{
		CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLConnection::SetCharset", "setting charset \"%s\"", charset.c_str());
		if (m_IsConnected && !charset.empty())
		{
			int error = mysql_set_character_set(m_Connection, charset.c_str());
			if (error == 0)
				CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLConnection::SetCharset", "charset \"%s\" has been set", charset.c_str());
			else
				CLog::Get()->LogFunction(LOG_ERROR, "CMySQLConnection::SetCharset", "error %d: %s", error, mysql_error(m_Connection));
		}
		else
			CLog::Get()->LogFunction(LOG_ERROR, "CMySQLConnection::SetCharset", "invalid charset (\"%s\") or not connected", charset.c_str());
	}
	return true;
}

void CMySQLConnection::QueueFunction(const function<bool()> &func)
{
	{
		boost::lock_guard<boost::mutex> lockguard(m_FuncQueueMtx);
		m_FuncQueue.push(func);
	}
	WakeUp();
}

void CMySQLConnection::WakeUp()
{
	// Taking the mutex orders this notification after the worker's predicate
	// check, so a wake-up cannot be lost between the check and the wait.
	{
		boost::lock_guard<boost::mutex> lockguard(m_WakeMtx);
	}
	m_WakeCond.notify_one();
}

bool CMySQLConnection::HasPendingFunctions()
{
	boost::lock_guard<boost::mutex> lockguard(m_FuncQueueMtx);
	return !m_FuncQueue.empty();
}

bool CMySQLConnection::HasPendingWork()
{
	return !m_QueryThreadRunning
		|| HasPendingFunctions()
		|| (m_IsConnected && m_QueryQueue.read_available() > 0);
}

void CMySQLConnection::ScheduleReconnect(bool failed)
{
	if (!failed)
	{
		m_ReconnectDelayMs = RECONNECT_DELAY_MIN_MS;
		return;
	}

	m_NextReconnect = chrono::steady_clock::now() + chrono::milliseconds(m_ReconnectDelayMs);
	if (m_QueryThread != NULL && m_AutoReconnect)
		CLog::Get()->LogFunction(LOG_WARNING, "CMySQLConnection::Connect", "retrying connection in %u ms", m_ReconnectDelayMs);

	m_ReconnectDelayMs *= 2;
	if (m_ReconnectDelayMs > RECONNECT_DELAY_MAX_MS)
		m_ReconnectDelayMs = RECONNECT_DELAY_MAX_MS;
}

void CMySQLConnection::ProcessQueries()
{
	mysql_thread_init();
	while(m_QueryThreadRunning)
	{
		queue<function<bool()> > functions;
		{
			boost::lock_guard<boost::mutex> lockguard(m_FuncQueueMtx);
			functions.swap(m_FuncQueue);
		}
		while (!functions.empty())
		{
			functions.front()();
			functions.pop();
		}

		// Keep retrying in the background when the server was unreachable at
		// startup or a reconnect failed; queued queries wait instead of stalling
		// forever.
		const bool wantsReconnect = !m_IsConnected && m_AutoReconnect && m_ConnectRequested;
		if (wantsReconnect && chrono::steady_clock::now() >= m_NextReconnect)
		{
			CLog::Get()->LogFunction(LOG_WARNING, "CMySQLConnection::ProcessQueries", "not connected, trying to reconnect...");
			Connect();
		}

		CMySQLQuery *query;
		while(m_QueryThreadRunning && m_IsConnected && m_QueryQueue.pop(query))
		{
			if(query->Execute(m_Connection) == false)
			{
				const unsigned int error = mysql_errno(m_Connection);
				if (m_AutoReconnect && (error == ERROR_SERVER_GONE || error == ERROR_SERVER_LOST))
				{
					CLog::Get()->LogFunction(LOG_WARNING, "CMySQLConnection::ProcessQueries", "lost connection, reconnecting...");
					Reconnect();
				}
			}
			if (!CCallback::Get()->QueueQuery(query))
			{
				CLog::Get()->LogFunction(LOG_ERROR, "CMySQLConnection::ProcessQueries", "callback queue is full");
				delete query;
			}
		}

		unsigned int waitMs = IDLE_WAIT_MS;
		if (!m_IsConnected && m_AutoReconnect && m_ConnectRequested)
		{
			const chrono::steady_clock::time_point now = chrono::steady_clock::now();
			if (m_NextReconnect <= now)
				waitMs = 0;
			else
			{
				const long long untilReconnect = chrono::duration_cast<chrono::milliseconds>(m_NextReconnect - now).count() + 1;
				if (untilReconnect < waitMs)
					waitMs = static_cast<unsigned int>(untilReconnect);
			}
		}

		if (waitMs > 0)
		{
			boost::unique_lock<boost::mutex> lock(m_WakeMtx);
			m_WakeCond.wait_for(lock, chrono::milliseconds(waitMs), boost::bind(&CMySQLConnection::HasPendingWork, this));
		}
	}
	mysql_thread_end();
}
