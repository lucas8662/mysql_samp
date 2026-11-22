#pragma once
#ifndef INC_CLOG_H
#define INC_CLOG_H

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

using std::string;

enum e_LogLevel
{
	LOG_NONE = 0,
	LOG_ERROR = 1,
	LOG_WARNING = 2,
	LOG_DEBUG = 4
};

enum e_LogType
{
	LOG_TYPE_TEXT = 1,
	LOG_TYPE_HTML = 2
};

enum e_LogInfo
{
	LOG_INFO_NONE,
	LOG_INFO_CALLBACK_BEGIN,
	LOG_INFO_CALLBACK_END,
	LOG_INFO_THREADED
};

class CLog
{
public:
	static inline CLog *Get()
	{
		if (m_Instance == NULL)
			m_Instance = new CLog;
		return m_Instance;
	}
	static inline void Destroy()
	{
		delete m_Instance;
		m_Instance = NULL;
	}

	void Initialize(const char *logfile);
	int LogFunction(unsigned loglevel, const char *funcname, const char *msg, ...);
	int LogText(unsigned int loglevel, const char *text);
	void StartCallback(const char *cbname);
	void EndCallback();

	void SetLogLevel(unsigned int loglevel)
	{
		m_LogLevel = loglevel;
	}
	bool IsLogLevel(unsigned int loglevel)
	{
		return !!(m_LogLevel & loglevel);
	}
	void SetLogType(unsigned int logtype);

private:
	static CLog *m_Instance;

	struct m_SLogData
	{
		m_SLogData() : Status(LOG_NONE), Info(LOG_INFO_NONE) {}
		unsigned int Status;
		string Name;
		string Msg;
		unsigned int Info;
	};

	CLog() :
		m_LogFileName("mysql_log.txt"),
		m_LogType(LOG_TYPE_TEXT),
		m_LogLevel(LOG_ERROR | LOG_WARNING),
		m_LogThreadAlive(true)
	{}
	~CLog();

	void ProcessLog();
	void QueueLog(m_SLogData &&logData);

	string m_LogFileName; //guarded by m_TextLogMtx once threads exist
	std::atomic<unsigned int> m_LogType;
	std::atomic<unsigned int> m_LogLevel;

	std::thread m_LogThread; //HTML log writer, started on demand
	std::atomic<bool> m_LogThreadAlive;
	std::thread::id m_MainThreadID;

	std::mutex m_LogQueueMtx;
	std::mutex m_TextLogMtx; //serializes writes to the text log file
	std::deque<m_SLogData> m_LogQueue;
};

#endif // INC_CLOG_H
