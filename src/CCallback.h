#pragma once
#ifndef INC_CCALLBACK_H
#define INC_CCALLBACK_H


#include <stack>
#include <string>
#include <variant>
#include <map>
#include <memory>
#include <deque>
#include <mutex>

using std::stack;
using std::string;

#include "main.h"


class CMySQLHandle;
class CMySQLQuery;


class CCallback 
{
private:
	static CCallback *m_Instance;


	// Filled by every query thread (main + pool), drained by ProcessTick.
	// Unbounded: a query that already ran on the server must never lose its
	// callback because the server thread fell behind.
	std::mutex m_CallbackQueueMtx;
	std::deque<std::unique_ptr<CMySQLQuery> > m_CallbackQueue;
	std::unique_ptr<CMySQLQuery> PopQuery();

	std::map<AMX *, std::shared_ptr<void> > m_AmxList;

	CCallback() { }
	~CCallback();

public:
	static inline CCallback *Get()
	{
		return m_Instance;
	}
	inline void Destroy()
	{
		m_Instance = NULL;
		delete this;
	}


	void FillCallbackParams(stack< std::variant<cell, string> > &dest, const char *format, AMX* amx, cell* params, const int ConstParamCount);


	void QueueQuery(std::unique_ptr<CMySQLQuery> query);

	inline void AddAmx(AMX *amx)
	{
		m_AmxList[amx] = std::make_shared<int>(0);
	}
	inline void EraseAmx(AMX *amx)
	{
		m_AmxList.erase(amx);
	}

	void BindOwner(CMySQLQuery *query, AMX *amx);
	void ClearByHandle(CMySQLHandle *handle);

	void ProcessCallbacks();
};


#endif // INC_CCALLBACK_H
