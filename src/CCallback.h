#pragma once
#ifndef INC_CCALLBACK_H
#define INC_CCALLBACK_H


#include <stack>
#include <string>
#include <boost/variant.hpp>
#include <map>
#include <memory>
#include <boost/lockfree/queue.hpp>

using std::stack;
using std::string;

#include "main.h"


class CMySQLHandle;
class CMySQLQuery;


class CCallback 
{
private:
	static CCallback *m_Instance;


	boost::lockfree::queue<
		CMySQLQuery *,
		boost::lockfree::fixed_sized<true>,
		boost::lockfree::capacity<32648>
	> m_CallbackQueue;

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


	void FillCallbackParams(stack< boost::variant<cell, string> > &dest, const char *format, AMX* amx, cell* params, const int ConstParamCount);


	inline bool QueueQuery(CMySQLQuery *query)
	{
		return m_CallbackQueue.push(query);
	}

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
