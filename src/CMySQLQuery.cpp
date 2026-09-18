#include "CMySQLHandle.h"
#include "CMySQLResult.h"
#include "CMySQLQuery.h"
#include "COrm.h"
#include "CLog.h"

#include "misc.h"

#include <boost/chrono.hpp>
namespace chrono = boost::chrono;


static void SetPreparedQueryError(CMySQLQuery *query, char *log_funcname, unsigned int error_id, const char *error_str)
{
	CLog::Get()->LogFunction(LOG_ERROR, log_funcname, "(error #%d) %s (Prepared query: \"%s\")", error_id, error_str, query->Query.c_str());

	if (!query->Unthreaded)
	{
		query->Orm.Object = NULL;
		query->Orm.Type = 0;
		while (!query->Callback.Params.empty())
			query->Callback.Params.pop();

		query->Callback.Params.push(static_cast<cell>(error_id));
		query->Callback.Params.push(string(error_str));
		query->Callback.Params.push(query->Callback.Name);
		query->Callback.Params.push(query->Query);
		query->Callback.Params.push(static_cast<cell>(query->Handle->GetID()));
		query->Callback.Name = "OnQueryError";
	}
}

bool CMySQLQuery::StorePreparedResult(MYSQL_STMT *statement)
{
	MYSQL_RES *metadata = mysql_stmt_result_metadata(statement);
	if (metadata == NULL)
		return false;

	const unsigned int fieldCount = mysql_num_fields(metadata);
	vector<MYSQL_BIND> bindings(fieldCount);
	vector<unsigned long> lengths(fieldCount);
	vector<my_bool> isNull(fieldCount);
	vector<my_bool> errors(fieldCount);
	vector<char> placeholders(fieldCount);
	memset(bindings.empty() ? NULL : &bindings[0], 0, bindings.size() * sizeof(MYSQL_BIND));
	for (unsigned int field = 0; field < fieldCount; ++field)
	{
		bindings[field].buffer_type = MYSQL_TYPE_STRING;
		bindings[field].buffer = &placeholders[field];
		bindings[field].buffer_length = 1;
		bindings[field].length = &lengths[field];
		bindings[field].is_null = &isNull[field];
		bindings[field].error = &errors[field];
	}
	if (fieldCount > 0 && mysql_stmt_bind_result(statement, &bindings[0]) != 0)
	{
		mysql_free_result(metadata);
		return false;
	}
	if (mysql_stmt_store_result(statement) != 0)
	{
		mysql_free_result(metadata);
		return false;
	}

	vector< vector<string> > values;
	vector< vector<char> > nulls;
	int fetchResult;
	while ((fetchResult = mysql_stmt_fetch(statement)) != MYSQL_NO_DATA)
	{
		if (fetchResult != 0 && fetchResult != MYSQL_DATA_TRUNCATED)
		{
			mysql_stmt_free_result(statement);
			mysql_free_result(metadata);
			return false;
		}
		values.push_back(vector<string>(fieldCount));
		nulls.push_back(vector<char>(fieldCount, 0));
		for (unsigned int field = 0; field < fieldCount; ++field)
		{
			if (isNull[field])
			{
				nulls.back()[field] = 1;
				continue;
			}
			vector<char> value(lengths[field] + 1, '\0');
			MYSQL_BIND column;
			memset(&column, 0, sizeof(column));
			column.buffer_type = MYSQL_TYPE_STRING;
			column.buffer = &value[0];
			column.buffer_length = lengths[field];
			column.length = &lengths[field];
			if (mysql_stmt_fetch_column(statement, &column, field, 0) != 0)
			{
				mysql_stmt_free_result(statement);
				mysql_free_result(metadata);
				return false;
			}
			values.back()[field].assign(&value[0], lengths[field]);
		}
	}

	Result = new CMySQLResult;
	Result->m_WarningCount = mysql_stmt_warning_count(statement);
	Result->m_Rows = values.size();
	Result->m_Fields = fieldCount;
	Result->m_FieldNames.reserve(fieldCount);
	MYSQL_FIELD *field;
	while ((field = mysql_fetch_field(metadata)) != NULL)
		Result->m_FieldNames.push_back(field->name);

	vector<size_t> rowSizes(values.size());
	size_t memorySize = sizeof(char **) * values.size();
	for (size_t row = 0; row < values.size(); ++row)
	{
		size_t rowSize = sizeof(char *) * (fieldCount + 1);
		for (unsigned int column = 0; column < fieldCount; ++column)
			if (!nulls[row][column])
				rowSize += values[row][column].size() + 1;
		const size_t alignment = sizeof(char *);
		rowSize = (rowSize + alignment - 1) & ~(alignment - 1);
		rowSizes[row] = rowSize;
		memorySize += rowSize;
	}
	Result->m_Data = values.empty() ? NULL : static_cast<char ***>(malloc(memorySize));
	if (!values.empty() && Result->m_Data == NULL)
	{
		delete Result;
		Result = NULL;
		mysql_stmt_free_result(statement);
		mysql_free_result(metadata);
		return false;
	}
	char *storage = values.empty() ? NULL : reinterpret_cast<char *>(&Result->m_Data[values.size()]);
	for (size_t row = 0; row < values.size(); ++row)
	{
		Result->m_Data[row] = reinterpret_cast<char **>(storage);
		char *fieldStorage = storage + sizeof(char *) * (fieldCount + 1);
		for (unsigned int column = 0; column < fieldCount; ++column)
		{
			if (nulls[row][column])
				Result->m_Data[row][column] = NULL;
			else
			{
				Result->m_Data[row][column] = fieldStorage;
				memcpy(fieldStorage, values[row][column].data(), values[row][column].size());
				fieldStorage[values[row][column].size()] = '\0';
				fieldStorage += values[row][column].size() + 1;
			}
		}
		Result->m_Data[row][fieldCount] = NULL;
		storage += rowSizes[row];
	}
	mysql_stmt_free_result(statement);
	mysql_free_result(metadata);
	return true;
}

bool CMySQLQuery::ExecutePrepared(MYSQL *mysql_connection)
{
	char log_funcname[64];
	if (Unthreaded)
		sprintf(log_funcname, "CMySQLQuery::ExecutePrepared");
	else
		sprintf(log_funcname, "CMySQLQuery::ExecutePrepared[%s]", Callback.Name.c_str());

	MYSQL_STMT *statement = mysql_stmt_init(mysql_connection);
	if (statement == NULL)
	{
		SetPreparedQueryError(this, log_funcname, mysql_errno(mysql_connection), mysql_error(mysql_connection));
		if (!Unthreaded)
			Handle->DecreaseQueryCounter();
		return false;
	}

	bool success = false;
	if (mysql_stmt_prepare(statement, Query.c_str(), static_cast<unsigned long>(Query.length())) != 0)
	{
		SetPreparedQueryError(this, log_funcname, mysql_stmt_errno(statement), mysql_stmt_error(statement));
	}
	else if (mysql_stmt_param_count(statement) != StatementParameters.size())
	{
		CLog::Get()->LogFunction(LOG_ERROR, log_funcname, "expected %d bound parameters, received %d (Prepared query: \"%s\")", static_cast<int>(mysql_stmt_param_count(statement)), static_cast<int>(StatementParameters.size()), Query.c_str());
		SetPreparedQueryError(this, log_funcname, 2031, "prepared statement parameter count does not match");
	}
	else
	{
		vector<MYSQL_BIND> bindings(StatementParameters.size());
		vector<unsigned long> lengths(StatementParameters.size());
		memset(bindings.empty() ? NULL : &bindings[0], 0, bindings.size() * sizeof(MYSQL_BIND));

		for (size_t index = 0; index < StatementParameters.size(); ++index)
		{
			s_StatementParameter &parameter = StatementParameters[index];
			MYSQL_BIND &binding = bindings[index];
			switch (parameter.Type)
			{
				case s_StatementParameter::TYPE_INTEGER:
					binding.buffer_type = MYSQL_TYPE_LONG;
					binding.buffer = &parameter.Integer;
					break;
				case s_StatementParameter::TYPE_FLOAT:
					binding.buffer_type = MYSQL_TYPE_FLOAT;
					binding.buffer = &parameter.Float;
					break;
				case s_StatementParameter::TYPE_STRING:
					lengths[index] = static_cast<unsigned long>(parameter.String.length());
					binding.buffer_type = MYSQL_TYPE_STRING;
					binding.buffer = const_cast<char *>(parameter.String.c_str());
					binding.buffer_length = lengths[index];
					binding.length = &lengths[index];
					break;
			}
		}

		if (!bindings.empty() && mysql_stmt_bind_param(statement, &bindings[0]) != 0)
		{
			SetPreparedQueryError(this, log_funcname, mysql_stmt_errno(statement), mysql_stmt_error(statement));
		}
		else if (mysql_stmt_execute(statement) != 0)
		{
			SetPreparedQueryError(this, log_funcname, mysql_stmt_errno(statement), mysql_stmt_error(statement));
		}
		else
		{
			if (mysql_stmt_field_count(statement) == 0 && (Unthreaded || Callback.Name.length() > 0))
			{
				Result = new CMySQLResult;
				Result->m_WarningCount = mysql_stmt_warning_count(statement);
				Result->m_AffectedRows = mysql_stmt_affected_rows(statement);
				Result->m_InsertID = mysql_stmt_insert_id(statement);
				Result->m_Query = Query;
				success = true;
			}
			else if (mysql_stmt_field_count(statement) != 0 && (Unthreaded || Callback.Name.length() > 0))
			{
				success = StorePreparedResult(statement);
				if (success)
					Result->m_Query = Query;
				else
					SetPreparedQueryError(this, log_funcname, mysql_stmt_errno(statement), mysql_stmt_error(statement));
			}
			else
			{
				mysql_stmt_free_result(statement);
				success = true;
			}
		}
	}

	mysql_stmt_close(statement);
	if (!Unthreaded)
		Handle->DecreaseQueryCounter();
	return success;
}

bool CMySQLQuery::Execute(MYSQL *mysql_connection)
{
	if (IsPreparedStatement)
		return ExecutePrepared(mysql_connection);

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
