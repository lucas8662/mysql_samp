#include "CMySQLHandle.h"
#include "CMySQLResult.h"
#include "CMySQLQuery.h"
#include "COrm.h"
#include "CLog.h"

#include "misc.h"

#include <boost/chrono.hpp>
namespace chrono = boost::chrono;


bool CMySQLQuery::Execute(MYSQL *mysql_connection)
{
	bool ret_val = false;
	char log_funcname[64];
	if (Unthreaded)
		sprintf(log_funcname, "CMySQLQuery::Execute");
	else
		sprintf(log_funcname, "CMySQLQuery::Execute[%s]", Callback.Name.c_str());

	CLog::Get()->LogFunction(LOG_DEBUG, log_funcname, "starting query execution");

	chrono::steady_clock::time_point query_exec = chrono::steady_clock::now();
	int query_error = mysql_real_query(mysql_connection, Query.c_str(), Query.length());
	chrono::steady_clock::duration query_exec_duration = chrono::steady_clock::now() - query_exec;
	
	if (query_error == 0)
	{
		unsigned int 
			query_exec_time_milli = static_cast<unsigned int>(chrono::duration_cast<chrono::milliseconds>(query_exec_duration).count()),
			query_exec_time_micro = static_cast<unsigned int>(chrono::duration_cast<chrono::microseconds>(query_exec_duration).count());

		CLog::Get()->LogFunction(LOG_DEBUG, log_funcname, "query was successfully executed within %d.%d milliseconds", query_exec_time_milli, query_exec_time_micro-(query_exec_time_milli*1000));

		MYSQL_RES *mysql_result = mysql_store_result(mysql_connection); //this has to be here

		//why should we process the result if it won't and can't be used? (except for ORM objects and unthreaded queries)
		bool is_orm = Orm.Object != NULL && (Orm.Type == ORM_QUERYTYPE_SELECT || Orm.Type == ORM_QUERYTYPE_INSERT);
		if (Unthreaded || is_orm || Callback.Name.length() > 0)
		{
			if (StoreResult(mysql_connection, mysql_result) == false)
				CLog::Get()->LogFunction(LOG_ERROR, log_funcname, "an error occured while storing the result: (error #%d) \"%s\"", mysql_errno(mysql_connection), mysql_error(mysql_connection));
			else
			{
				Result->m_Query = Query;
				Result->m_ExecTime[UNIT_MILLISECONDS] = query_exec_time_milli;
				Result->m_ExecTime[UNIT_MICROSECONDS] = query_exec_time_micro;
			}
		}
		else  //no callback was specified
			CLog::Get()->LogFunction(LOG_DEBUG, log_funcname, "no callback specified, skipping result saving");

		if (mysql_result != NULL)
			mysql_free_result(mysql_result);

		//process all further result sets - just free them all to avoid desync
		while (mysql_next_result(mysql_connection) == 0)
			mysql_free_result(mysql_store_result(mysql_connection));

		ret_val = true;
	}
	else  //mysql_real_query failed
	{
		int error_id = mysql_errno(mysql_connection);
		string error_str(mysql_error(mysql_connection));

		CLog::Get()->LogFunction(LOG_ERROR, log_funcname, "(error #%d) %s (Query: \"%s\")", error_id, error_str.c_str(), Query.c_str());

		if (!Unthreaded)
		{
			//forward OnQueryError(error_id, error[], callback[], query[], connectionHandle);
			//recycle these structures, change some data

			Orm.Object = NULL;
			Orm.Type = 0;

			while (Callback.Params.size() > 0)
				Callback.Params.pop();


			Callback.Params.push(static_cast<cell>(error_id));
			Callback.Params.push(error_str);
			Callback.Params.push(Callback.Name);
			Callback.Params.push(Query);
			Callback.Params.push(static_cast<cell>(Handle->GetID()));

			Callback.Name = "OnQueryError";

			CLog::Get()->LogFunction(LOG_DEBUG, log_funcname, "error will be triggered in OnQueryError");
		}
		ret_val = false;
	}

	if(Unthreaded == false) //decrease counter only if threaded query
		Handle->DecreaseQueryCounter();
	return ret_val;
}


bool CMySQLQuery::StoreResult(MYSQL *mysql_connection, MYSQL_RES *mysql_result)
{
	if (mysql_result != NULL)
	{
		MYSQL_FIELD *mysql_field;
		MYSQL_ROW mysql_row;

		CMySQLResult *result_ptr = Result = new CMySQLResult;

		Result->m_WarningCount = mysql_warning_count(mysql_connection);

		const my_ulonglong num_rows = Result->m_Rows = mysql_num_rows(mysql_result);
		const unsigned int num_fields = Result->m_Fields = mysql_num_fields(mysql_result);

		Result->m_FieldNames.reserve(Result->m_Fields + 1);


		while (mysql_field = mysql_fetch_field(mysql_result))
		{
			Result->m_FieldNames.push_back(mysql_field->name);
		}

		// Result rows are owned by the connector and become invalid after
		// mysql_free_result().  Copy each field using its documented length
		// instead of depending on the connector's internal row layout.
		vector<size_t> rowSizes(static_cast<size_t>(num_rows));
		size_t memSize = sizeof(char **) * static_cast<size_t>(num_rows);

		for (size_t r = 0; r != num_rows; ++r)
		{
			mysql_row = mysql_fetch_row(mysql_result);
			unsigned long* lengths = mysql_fetch_lengths(mysql_result);
			if (mysql_row == NULL || lengths == NULL)
				return false;

			size_t rowSize = sizeof(char *) * (num_fields + 1);
			for (size_t f = 0; f != num_fields; ++f)
			{
				if (mysql_row[f] != NULL)
					rowSize += lengths[f] + 1;
			}
			const size_t alignment = sizeof(char *);
			rowSize = (rowSize + alignment - 1) & ~(alignment - 1);
			rowSizes[r] = rowSize;
			memSize += rowSize;
		}

		char ***memData = result_ptr->m_Data = static_cast<char ***>(malloc(memSize));
		if (memData == NULL)
			return false;
		char* rowStorage = reinterpret_cast<char*>(&memData[num_rows]);

		mysql_data_seek(mysql_result, 0);
		for (size_t r = 0; r != num_rows; ++r)
		{
			mysql_row = mysql_fetch_row(mysql_result);
			unsigned long* lengths = mysql_fetch_lengths(mysql_result);
			if (mysql_row == NULL || lengths == NULL)
				return false;

			memData[r] = reinterpret_cast<char**>(rowStorage);
			char* valueStorage = rowStorage + sizeof(char *) * (num_fields + 1);
			for (size_t f = 0; f != num_fields; ++f)
			{
				if (mysql_row[f] == NULL)
				{
					memData[r][f] = NULL;
					continue;
				}
				memData[r][f] = valueStorage;
				memcpy(valueStorage, mysql_row[f], lengths[f]);
				valueStorage[lengths[f]] = '\0';
				valueStorage += lengths[f] + 1;
			}
			memData[r][num_fields] = NULL;
			rowStorage += rowSizes[r];
		}
		return true;
	}
	else if (mysql_field_count(mysql_connection) == 0) //query is non-SELECT query
	{
		Result = new CMySQLResult;
		Result->m_WarningCount = mysql_warning_count(mysql_connection);
		Result->m_AffectedRows = mysql_affected_rows(mysql_connection);
		Result->m_InsertID = mysql_insert_id(mysql_connection);
		return true;
	}
	else //error
	{
		//we clear the callback name and forward it to the callback handler
		//the callback handler free's all memory but doesn't call the callback because there's no callback name
		Callback.Name.clear();
		return false;
	}
}
