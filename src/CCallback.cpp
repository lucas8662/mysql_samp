#include "main.h"
#include "CCallback.h"
#include "CMySQLHandle.h"
#include "CMySQLQuery.h"
#include "CMySQLResult.h"
#include "COrm.h"
#include "CLog.h"
#include "misc.h"



CCallback *CCallback::m_Instance = new CCallback;

CCallback::~CCallback() = default;

void CCallback::QueueQuery(std::unique_ptr<CMySQLQuery> query)
{
	std::lock_guard<std::mutex> lock(m_CallbackQueueMtx);
	m_CallbackQueue.push_back(std::move(query));
}

// Pops one entry at a time and never holds the lock while a callback runs:
// a callback may call mysql_close(), which re-enters ClearByHandle().
std::unique_ptr<CMySQLQuery> CCallback::PopQuery()
{
	std::lock_guard<std::mutex> lock(m_CallbackQueueMtx);
	if (m_CallbackQueue.empty())
		return nullptr;
	std::unique_ptr<CMySQLQuery> query = std::move(m_CallbackQueue.front());
	m_CallbackQueue.pop_front();
	return query;
}

void CCallback::BindOwner(CMySQLQuery *query, AMX *amx)
{
	query->Callback.Owner = amx;
	auto owner = m_AmxList.find(amx);
	if (owner != m_AmxList.end())
		query->Callback.Lifetime = owner->second;
}

void CCallback::ProcessCallbacks() 
{
	while (std::unique_ptr<CMySQLQuery> query = PopQuery())
	{
		// ORM destruction/script unload cancels application, not SQL already sent.
		if (query->Orm.Cancelled() || (query->Callback.Owner != NULL && query->Callback.Lifetime.expired()))
			continue;
		if (query->Orm.Object != NULL)
		{
			switch (query->Orm.Type)
			{
			case ORM_QUERYTYPE_SELECT:
				query->Orm.Object->ApplySelectResult(query->Result);
				break;

			case ORM_QUERYTYPE_INSERT:
				query->Orm.Object->ApplyInsertResult(query->Result);
				break;
			}
		}

		if (!query->Callback.Name.empty())
		{
			const bool pass_by_ref = (query->Callback.Name.find("FJ37DH3JG") != string::npos);
			if (query->Callback.Owner != NULL && !query->Callback.Lifetime.expired())
			{
				AMX *amx = query->Callback.Owner;
				int amx_index = -1;

				if (amx_FindPublic(amx, query->Callback.Name.c_str(), &amx_index) == AMX_ERR_NONE && amx_index >= 0)
				{
					cell amx_mem_addr = -1;
					CLog::Get()->StartCallback(query->Callback.Name.c_str());

					while (!query->Callback.Params.empty())
					{
						std::variant<cell, string> value = std::move(query->Callback.Params.top());
						if (cell *number = std::get_if<cell>(&value))
						{
							if (pass_by_ref)
							{
								cell tmp_addr;
								amx_PushArray(amx, &tmp_addr, NULL, number, 1);
								if (amx_mem_addr < 0)
									amx_mem_addr = tmp_addr;
							}
							else
								amx_Push(amx, *number);
						}
						else
						{
							cell tmp_addr;
							amx_PushString(amx, &tmp_addr, NULL, std::get<string>(value).c_str(), 0, 0);
							if (amx_mem_addr < 0)
								amx_mem_addr = tmp_addr;
						}

						query->Callback.Params.pop();
					}

					query->Handle->SetActiveResult(query->Result);
					query->Result = NULL;

					const std::weak_ptr<void> handleLifetime = query->Handle->GetLifetime();
					cell amx_ret;
					amx_Exec(amx, &amx_ret, amx_index);
					if (!query->Callback.Lifetime.expired() && amx_mem_addr >= 0)
						amx_Release(amx, amx_mem_addr);

					if (!handleLifetime.expired())
					{
						if (!query->Handle->IsActiveResultSaved())
							delete query->Handle->GetActiveResult();
						query->Handle->SetActiveResult(static_cast<CMySQLResult *>(NULL));
					}

					CLog::Get()->EndCallback();
				}
			}
		}

	}
}


void CCallback::FillCallbackParams(stack< std::variant<cell, string> > &dest, const char *format, AMX* amx, cell* params, const int ConstParamCount)
{
	if (format == NULL || !(*format))
		return ;

	unsigned int ParamIdx = 1;
	cell *AddressPtr = NULL;

	do
	{
		char *StrBuf = NULL;
		switch (*format)
		{
			case 'd':
			case 'i':
			case 'f':
				amx_GetAddr(amx, params[ConstParamCount + ParamIdx], &AddressPtr);
				dest.push(*AddressPtr);
				break;
			case 'z':
			case 's':
				amx_StrParam(amx, params[ConstParamCount + ParamIdx], StrBuf);
				dest.push(StrBuf == NULL ? string() : string(StrBuf));
				break;
			default:
				dest.push(string("NULL"));
		}
		ParamIdx++;
	} while (*(++format));
}

void CCallback::ClearByHandle(CMySQLHandle *handle)
{
	// Destroyed after the lock is released: a query's destructor may log.
	std::deque<std::unique_ptr<CMySQLQuery> > removed;
	std::lock_guard<std::mutex> lock(m_CallbackQueueMtx);
	for (auto it = m_CallbackQueue.begin(); it != m_CallbackQueue.end();)
	{
		if ((*it)->Handle == handle)
		{
			removed.push_back(std::move(*it));
			it = m_CallbackQueue.erase(it);
		}
		else
			++it;
	}
}
