#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <vector>

#include <boost/chrono.hpp>

#include "CLog.h"

namespace chrono = boost::chrono;

CLog *CLog::m_Instance = NULL;

static string FormatLogMessage(const char *format, va_list arguments)
{
	if (format == NULL)
		return string();

	va_list copy;
	va_copy(copy, arguments);
	const int size = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (size < 0)
		return "unable to format log message";

	std::vector<char> buffer(static_cast<size_t>(size) + 1);
	vsnprintf(&buffer[0], buffer.size(), format, arguments);
	return string(&buffer[0], static_cast<size_t>(size));
}

static string EscapeJavaScriptString(const string& value)
{
	string escaped;
	escaped.reserve(value.size());
	for (size_t index = 0; index < value.size(); ++index)
	{
		switch (value[index])
		{
			case '\\': escaped.append("\\\\"); break;
			case '"': escaped.append("\\\""); break;
			case '\n': escaped.append("\\n"); break;
			case '\r': escaped.append("\\r"); break;
			case '\t': escaped.append("\\t"); break;
			case '<': escaped.append("\\x3C"); break;
			case '>': escaped.append("\\x3E"); break;
			case '&': escaped.append("\\x26"); break;
			default: escaped.push_back(value[index]); break;
		}
	}
	return escaped;
}

static const char *LogLevelName(unsigned int level)
{
	switch (level)
	{
		case LOG_ERROR: return "ERROR";
		case LOG_WARNING: return "WARNING";
		case LOG_DEBUG: return "DEBUG";
		default: return "LOG";
	}
}

void CLog::ProcessLog()
{
	FILE *logFile = fopen(m_LogFileName.c_str(), "w");
	if (logFile == NULL)
		return;

	char startTime[32];
	time_t rawTime;
	time(&rawTime);
	strftime(startTime, sizeof(startTime), "%H:%M, %d.%m.%Y", localtime(&rawTime));
	fprintf(logFile,
		"<!doctype html><html><head><meta charset=\"utf-8\"><title>MySQL Plugin log</title>"
		"<style>body{font-family:Arial;background:#ddd}table{border-collapse:collapse;width:100%%}th,td{border:1px solid #333;padding:4px;word-break:break-word}.ERROR{background:#f99}.WARNING{background:#fc9}.DEBUG{background:#9f9}</style>"
		"<script>function Log(t,f,s,m){var r=document.createElement('tr');r.className=s;[t,f,s,m].forEach(function(v){var c=document.createElement('td');c.textContent=v;r.appendChild(c);});document.getElementById('log').appendChild(r);}function StartCB(n){var r=document.createElement('tr'),c=document.createElement('td');c.colSpan=4;c.textContent='Callback: '+n;r.appendChild(c);document.getElementById('log').appendChild(r);}</script>"
		"</head><body><h2>Logging started at %s</h2><table><thead><tr><th>Time</th><th>Function</th><th>Status</th><th>Message</th></tr></thead><tbody id=\"log\"></tbody></table>\n",
		startTime);
	fflush(logFile);

	while (m_LogThreadAlive)
	{
		m_SLogData *logData = NULL;
		while (m_LogQueue.pop(logData))
		{
			if (logData->Info == LOG_INFO_CALLBACK_BEGIN)
			{
				const string callback = EscapeJavaScriptString(logData->Msg);
				fprintf(logFile, "<script>StartCB(\"%s\");</script>\n", callback.c_str());
			}
			else if (logData->Info != LOG_INFO_CALLBACK_END)
			{
				char timeform[16];
				time_t now;
				time(&now);
				strftime(timeform, sizeof(timeform), "%X", localtime(&now));
				const string functionName = EscapeJavaScriptString(logData->Name);
				const string message = EscapeJavaScriptString(logData->Msg);
				fprintf(logFile, "<script>Log(\"%s\",\"%s\",\"%s\",\"%s\");</script>\n",
					timeform, functionName.c_str(), LogLevelName(logData->Status), message.c_str());
			}
			delete logData;
		}
		fflush(logFile);
		this_thread::sleep_for(chrono::milliseconds(10));
	}

	fputs("</body></html>\n", logFile);
	fclose(logFile);
}

void CLog::Initialize(const char *logfile)
{
	m_LogFileName = logfile != NULL && *logfile != '\0' ? logfile : "mysql_log.txt";
	SetLogType(m_LogType);
	m_MainThreadID = this_thread::get_id();
}

void CLog::SetLogType(unsigned int logtype)
{
	if (logtype != LOG_TYPE_HTML && logtype != LOG_TYPE_TEXT)
		return;
	if (logtype == m_LogType)
		return;

	m_LogType = logtype;
	const size_t extension = m_LogFileName.find_last_of('.');
	const string baseName = extension == string::npos ? m_LogFileName : m_LogFileName.substr(0, extension);
	m_LogFileName = baseName + (logtype == LOG_TYPE_HTML ? ".html" : ".txt");

	if (logtype == LOG_TYPE_HTML && m_LogThread == NULL)
		m_LogThread = new thread(&CLog::ProcessLog, this);
}

int CLog::LogFunction(unsigned int loglevel, const char *funcname, const char *msg, ...)
{
	if (m_LogLevel == LOG_NONE || !(m_LogLevel & loglevel))
		return 0;

	va_list arguments;
	va_start(arguments, msg);
	const string formatted = FormatLogMessage(msg, arguments);
	va_end(arguments);
	const string functionName = funcname != NULL ? funcname : "unknown";

	if (m_LogType == LOG_TYPE_HTML)
	{
		m_SLogData *logData = new m_SLogData;
		logData->Info = this_thread::get_id() != m_MainThreadID ? LOG_INFO_THREADED : LOG_INFO_NONE;
		logData->Status = loglevel;
		logData->Name = functionName;
		logData->Msg = formatted;
		if (!m_LogQueue.push(logData))
			delete logData;
	}
	else
	{
		const string text = functionName + " - " + formatted;
		LogText(loglevel, text.c_str());
	}
	return 0;
}

int CLog::LogText(unsigned int loglevel, const char *text)
{
	if (!(m_LogLevel & loglevel))
		return 0;

	char timeform[64];
	time_t rawtime;
	time(&rawtime);
	strftime(timeform, sizeof(timeform), "%X %x", localtime(&rawtime));
	FILE *logFile = fopen(m_LogFileName.c_str(), "a");
	if (logFile != NULL)
	{
		fprintf(logFile, "[%s] [%s] %s\n", timeform, LogLevelName(loglevel), text != NULL ? text : "");
		fclose(logFile);
	}
	return 0;
}

void CLog::StartCallback(const char *cbname)
{
	if (m_LogLevel == LOG_NONE || m_LogType != LOG_TYPE_HTML)
		return;
	m_SLogData *logData = new m_SLogData;
	logData->Info = LOG_INFO_CALLBACK_BEGIN;
	logData->Msg = cbname != NULL ? cbname : "";
	if (!m_LogQueue.push(logData))
		delete logData;
}

void CLog::EndCallback()
{
	if (m_LogLevel == LOG_NONE || m_LogType != LOG_TYPE_HTML)
		return;
	m_SLogData *logData = new m_SLogData;
	logData->Info = LOG_INFO_CALLBACK_END;
	if (!m_LogQueue.push(logData))
		delete logData;
}

CLog::~CLog()
{
	if (m_LogThread != NULL)
	{
		m_LogThreadAlive = false;
		m_LogThread->join();
		delete m_LogThread;
		m_LogThread = NULL;
	}

	m_SLogData *logData = NULL;
	while (m_LogQueue.pop(logData))
		delete logData;
}
