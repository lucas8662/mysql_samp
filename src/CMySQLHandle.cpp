#include "CMySQLHandle.h"
#include "CMySQLResult.h"
#include "CMySQLQuery.h"
#include "CMySQLConnection.h"
#include "CQueryQueue.h"
#include "CLog.h"
#include "COrm.h"
#include "IdAllocator.h"




unordered_map<unsigned int, CMySQLHandle *> CMySQLHandle::SQLHandle;
CMySQLHandle *CMySQLHandle::ActiveHandle = NULL;
CMySQLOptions MySQLOptions;


CMySQLHandle::CMySQLHandle(unsigned int id) : 
	m_QueryCounter(0),

	m_ActiveResult(NULL),
	m_ActiveResultID(0),
	m_NextResultID(1),

	m_MyID(id),

	m_WarnedNoPool(false),

	m_MainConnection(NULL),
	m_ThreadConnection(NULL)
{
	CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::CMySQLHandle", "constructor called");
}

CMySQLHandle::~CMySQLHandle() 
{
	m_Lifetime.reset();
	for (unordered_map<unsigned int, CMySQLResult*>::iterator it = m_SavedResults.begin(), end = m_SavedResults.end(); it != end; it++)
		delete it->second;
	if (m_ActiveResultID == 0)
		delete m_ActiveResult;
	
	// Joins every query thread, so nothing consumes the queues any more.
	ExecuteOnConnections([](CMySQLConnection *connection) { connection->Destroy(); });
	m_ConnectionPool.clear();
	if (m_ThreadQueue)
		m_ThreadQueue->Clear();
	if (m_PoolQueue)
		m_PoolQueue->Clear();

	CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::~CMySQLHandle", "deconstructor called");
}

CMySQLHandle *CMySQLHandle::Create(string host, string user, string pass, string db, size_t port, size_t pool_size, bool reconnect, const CMySQLTLSOptions& tls) 
{
	if (port == 0 || port > 65535 || pool_size > 64)
	{
		CLog::Get()->LogFunction(LOG_ERROR, "CMySQLHandle::Create", "port must be 1..65535 and pool_size must be 0..64");
		return NULL;
	}
	CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::Create", "creating new connection..");

	CMySQLHandle *handle = NULL;
	CMySQLConnection *main_connection = CMySQLConnection::Create(host, user, pass, db, port, reconnect, nullptr, tls);

	if (MySQLOptions.DuplicateConnections == false && SQLHandle.size() > 0) 
	{
		//code used for checking duplicate connections
		for(unordered_map<unsigned int, CMySQLHandle*>::iterator i = SQLHandle.begin(), end = SQLHandle.end(); i != end; ++i) 
		{
			CMySQLConnection *Connection = i->second->m_MainConnection;
			if((*Connection) == (*main_connection))
			{
				CLog::Get()->LogFunction(LOG_WARNING, "CMySQLHandle::Create", "connection already exists");
				handle = i->second;
				break;
			}
		}
	}

	if(handle == NULL) 
	{
		const unsigned int id = GetLowestFreeId(SQLHandle);


		handle = new CMySQLHandle(id);

		//init connections
		handle->m_MainConnection = main_connection;
		handle->m_ThreadQueue.reset(new CQueryQueue);
		const string prefix = std::to_string(id) + "/";
		handle->m_ThreadConnection = CMySQLConnection::Create(host, user, pass, db, port, reconnect, handle->m_ThreadQueue.get(), tls, prefix + "tquery");

		if (pool_size > 0)
		{
			handle->m_PoolQueue.reset(new CQueryQueue);
			for (size_t i = 0; i < pool_size; ++i)
				handle->m_ConnectionPool.insert(CMySQLConnection::Create(host, user, pass, db, port, reconnect, handle->m_PoolQueue.get(), tls,
					prefix + "pool#" + std::to_string(i + 1)));
		}

		SQLHandle.insert( unordered_map<unsigned int, CMySQLHandle*>::value_type(id, handle) );

		CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::Create", "connection created (id: %d)", id);
	}
	else
	{
		// The temporary connection used for duplicate detection is not owned by
		// the existing handle.
		main_connection->Destroy();
	}
	return handle;
}

void CMySQLHandle::Destroy() 
{
	COrm::ClearByHandle(this);
	if (ActiveHandle == this)
		ActiveHandle = NULL;

	SQLHandle.erase(m_MyID);
	delete this;
}

void CMySQLHandle::ExecuteOnConnections(const std::function<void (CMySQLConnection *)> &func)
{
	if (m_MainConnection != NULL)
		func(m_MainConnection);
	
	if (m_ThreadConnection != NULL)
		func(m_ThreadConnection);
	
	
	for(set<CMySQLConnection *>::iterator c = m_ConnectionPool.begin(), end = m_ConnectionPool.end(); c != end; ++c)
		func(*c);
}

bool CMySQLHandle::QueueQuery(std::unique_ptr<CMySQLQuery> query, bool use_pool /*= false*/)
{
	CQueryQueue *queue = NULL;
	if (use_pool && m_PoolQueue)
		queue = m_PoolQueue.get();
	else if (m_ThreadConnection != NULL)
	{
		queue = m_ThreadQueue.get();
		if (use_pool && !m_WarnedNoPool)
		{
			m_WarnedNoPool = true;
			CLog::Get()->LogFunction(LOG_WARNING, "CMySQLHandle::QueueQuery", "connection %d has no pool (pool_size = 0); mysql_pquery runs on the threaded connection", m_MyID);
		}
	}

	if (queue == NULL)
	{
		CLog::Get()->LogFunction(LOG_ERROR, "CMySQLHandle::QueueQuery", "no threaded connection available");
		return false;
	}
	// Count before queueing: the query thread decrements as soon as it runs it.
	const unsigned int pending = ++m_QueryCounter;
	if (CLog::Get()->IsLogLevel(LOG_DEBUG))
	{
		string text = query->IsTransaction ? "transaction" : query->Query;
		if (MySQLOptions.Log_TruncateData && text.length() > 64)
			text = text.substr(0, 64) + "...";
		CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::QueueQuery", "%s queued on %u/%s (%u unprocessed), callback \"%s\": \"%s\"",
			query->Tag().c_str(), m_MyID, queue == m_PoolQueue.get() ? "pool" : "tquery", pending,
			query->Callback.Name.c_str(), text.c_str());
	}
	query->Trace.Queued = CMySQLQuery::s_Trace::clock::now();
	queue->Push(std::move(query));
	return true;
}

unsigned int CMySQLHandle::SaveActiveResult() 
{
	if(m_ActiveResult != NULL) 
	{
		if(m_ActiveResultID != 0) //if active cache was already saved
		{
			CLog::Get()->LogFunction(LOG_WARNING, "CMySQLHandle::SaveActiveResult", "active cache was already saved");
			return m_ActiveResultID; //return the id of already saved cache
		}
		else 
		{
			const unsigned int id = GetNextFreeId(m_SavedResults, m_NextResultID);

			m_ActiveResultID = id;
			m_SavedResults.insert( unordered_map<unsigned int, CMySQLResult*>::value_type(id, m_ActiveResult) );
			
			CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::SaveActiveResult", "cache saved (id: %d)", id);
			return id; 
		}
	}
	
	return 0;
}

bool CMySQLHandle::DeleteSavedResult(unsigned int resultid) 
{
	if (IsValidResult(resultid))
	{
		CMySQLResult *ResultHandle = m_SavedResults.at(resultid);
		if(m_ActiveResult == ResultHandle) 
		{
			m_ActiveResult = NULL;
			m_ActiveResultID = 0;
			ActiveHandle = NULL;
		}
		delete ResultHandle;
		m_SavedResults.erase(resultid);
		CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::DeleteSavedResult", "result deleted");
		return true;
	}
	else
	{
		CLog::Get()->LogFunction(LOG_WARNING, "CMySQLHandle::DeleteSavedResult", "invalid result id ('%d')", resultid);
		return false;
	}
}

bool CMySQLHandle::SetActiveResult(unsigned int resultid) 
{
	if(resultid != 0) 
	{
		if(m_SavedResults.find(resultid) != m_SavedResults.end()) 
		{
			CMySQLResult *cResult = m_SavedResults.at(resultid);
			if(cResult != NULL) 
			{
				if(m_ActiveResult != NULL)
					if (m_ActiveResultID == 0) //if cache not saved
						delete m_ActiveResult; //delete unsaved cache
				
				m_ActiveResult = cResult; //set new active cache
				m_ActiveResultID = resultid; //new active cache was stored previously
				ActiveHandle = this;
				CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::SetActiveResult", "result is now active");
			}
		}
		else
			CLog::Get()->LogFunction(LOG_ERROR, "CMySQLHandle::SetActiveResult", "result not found");
	}
	else 
	{
		if (m_ActiveResultID == 0) //if cache not saved
			delete m_ActiveResult; //delete unsaved cache
		m_ActiveResult = NULL;
		m_ActiveResultID = 0;
		ActiveHandle = NULL;
		CLog::Get()->LogFunction(LOG_DEBUG, "CMySQLHandle::SetActiveResult", "invalid result id specified, setting active result to zero");
	}
	return true;
}

void CMySQLHandle::ClearAll()
{
	// Destroy() erases itself from SQLHandle, so iterating while destroying
	// invalidates the iterator. Pop one entry at a time instead.
	while (!SQLHandle.empty())
		SQLHandle.begin()->second->Destroy();
}

void CMySQLHandle::SetActiveResult(CMySQLResult *result)
{
	m_ActiveResult = result;
	m_ActiveResultID = 0;
	if (result != NULL)
		ActiveHandle = this;
	else
		ActiveHandle = NULL;
}
