#pragma once
#pragma warning (disable: 4996)

#include <cstdio>
#include <sstream>
#include <cmath>
#include <limits>
#include <locale>
#include <iomanip>
using std::ostringstream;


#include "COrm.h"
#include "CLog.h"
#include "CMySQLHandle.h"
#include "CMySQLResult.h"
#include "CMySQLConnection.h"

#include "misc.h"


unordered_map<unsigned int, COrm *> COrm::OrmHandle;

namespace {
string QuoteIdentifier(const string &name)
{
    string value("`");
    for (size_t i = 0; i < name.size(); ++i) {
        value += name[i];
        if (name[i] == '`') value += '`';
    }
    return value + '`';
}
}

void COrm::ClearByHandle(CMySQLHandle *handle)
{
    for (auto it = OrmHandle.begin(); it != OrmHandle.end();) {
        COrm *orm = (it++)->second;
        if (orm->m_ConnHandle == handle) orm->Destroy();
    }
}

void COrm::ClearByAmx(AMX *owner)
{
    for (auto it = OrmHandle.begin(); it != OrmHandle.end();) {
        COrm *orm = (it++)->second;
        if (orm->m_Owner == owner) orm->Destroy();
    }
}

bool COrm::GetVariableValue(const SVarInfo *var, string &value) const
{
    if (!var || !var->Address || !m_ConnHandle) return false;
    if (var->Datatype == DATATYPE_STRING) {
        if (!var->MaxLen) return false;
        vector<char> text(var->MaxLen, 0);
        if (amx_GetString(text.data(), var->Address, 0, text.size()) != AMX_ERR_NONE)
            return false;
        string escaped;
        if (!m_ConnHandle->GetMainConnection() ||
            !m_ConnHandle->GetMainConnection()->EscapeString(text.data(), escaped))
            return false;
        value = "'" + escaped + "'";
        return true;
    }
    ostringstream out;
    out.imbue(std::locale::classic());
    if (var->Datatype == DATATYPE_INT) out << static_cast<int>(*var->Address);
    else if (var->Datatype == DATATYPE_FLOAT) {
        const float number = amx_ctof(*var->Address);
        if (!std::isfinite(number)) return false;
        out << std::setprecision(std::numeric_limits<float>::max_digits10) << number;
    } else return false;
    value = out.str();
    return true;
}


unsigned int COrm::Create(const char *table, CMySQLHandle *connhandle, AMX *owner)
{
	CLog::Get()->LogFunction(LOG_DEBUG, "COrm::Create", "creating new orm object..");

	if(table == NULL || !*table)
		return CLog::Get()->LogFunction(LOG_ERROR, "COrm::Create", "empty table name specified");

	if(connhandle == NULL)
		return CLog::Get()->LogFunction(LOG_ERROR, "COrm::Create", "invalid connection handle");

	unsigned int id = 1;
	if(OrmHandle.size() > 0) 
	{
		unordered_map<unsigned int, COrm*>::iterator itHandle = OrmHandle.begin();
		do 
		{
			id = itHandle->first+1;
			++itHandle;
		} while(OrmHandle.find(id) != OrmHandle.end());
	}


	COrm *OrmObject = new COrm;
	OrmObject->m_ConnHandle = connhandle;
	OrmObject->m_TableName.assign(table);
	OrmObject->m_MyID = id;
	OrmObject->m_Owner = owner;

	OrmHandle.insert( unordered_map<int, COrm*>::value_type(id, OrmObject) );
	CLog::Get()->LogFunction(LOG_DEBUG, "COrm::Create", "orm object created (id: %d)", id);
	return id;
}

void COrm::Destroy() 
{
	CLog::Get()->LogFunction(LOG_DEBUG, "COrm::Destroy", "orm object destroyed (id: %d)", m_MyID);
	OrmHandle.erase(m_MyID);
	delete this;
}


bool COrm::ApplyActiveResult(unsigned int row) 
{
	if(m_ConnHandle == NULL)
	{
		CLog::Get()->LogFunction(LOG_ERROR, "COrm::ApplyActiveResult", "invalid connection handle");
		return false;
	}

	CMySQLResult *result = m_ConnHandle->GetActiveResult();
	
	m_ErrorID = ORM_ERROR_NO_DATA;
	if(result == NULL)
	{
		CLog::Get()->LogFunction(LOG_ERROR, "COrm::ApplyActiveResult", "no active result");
		return false;
	}

	if(row >= result->GetRowCount())
	{
		CLog::Get()->LogFunction(LOG_ERROR, "COrm::ApplyActiveResult", "invalid row specified");
		return false;
	}

	m_ErrorID = ORM_ERROR_OK;
	for(size_t v=0; v < m_Vars.size(); ++v) 
	{
		SVarInfo *var = m_Vars.at(v);

		const char *data = result->GetRowDataByName(row, var->Name.c_str());

		if(data != NULL) 
		{
			switch(var->Datatype) 
			{
				case DATATYPE_INT: 
				{
					int int_var = 0;
					if(ConvertStrToInt(data, int_var))
						(*var->Address) = int_var;
				} 
				break;
				case DATATYPE_FLOAT: 
				{
					float float_var = 0.0f;
					if(ConvertStrToFloat(data, float_var))
						(*var->Address) = amx_ftoc(float_var);

				} 
				break;
				case DATATYPE_STRING:
					amx_SetString(var->Address, data != NULL ? data : "NULL", 0, 0, var->MaxLen);
				break;
			}
		}
	}

	//also check for key in result
	if(m_KeyVar != NULL) 
	{
		const char *key_data = result->GetRowDataByName(row, m_KeyVar->Name.c_str());
		if(key_data != NULL) 
		{
			if(m_KeyVar->Datatype == DATATYPE_INT) 
			{
				int int_var = 0;
				if(ConvertStrToInt(key_data, int_var))
					(*(m_KeyVar->Address)) = int_var;
			}
			else if(m_KeyVar->Datatype == DATATYPE_STRING) 
				amx_SetString(m_KeyVar->Address, key_data, 0, 0, m_KeyVar->MaxLen);
		}
	}
	return true;
}

bool COrm::GenerateSelectQuery(string &dest)
{
    string key;
    if (!m_KeyVar || m_Vars.empty() || !GetVariableValue(m_KeyVar, key)) return false;
    string query("SELECT ");
    for (size_t i = 0; i < m_Vars.size(); ++i) {
        if (i) query += ',';
        query += QuoteIdentifier(m_Vars[i]->Name);
    }
    query += " FROM " + QuoteIdentifier(m_TableName) + " WHERE " +
        QuoteIdentifier(m_KeyVar->Name) + '=' + key + " LIMIT 1";
    dest.swap(query);
    return true;
}

void COrm::ApplySelectResult(CMySQLResult *result) 
{
	if(result == NULL || result->GetFieldCount() != m_Vars.size() || result->GetRowCount() != 1)
		m_ErrorID = ORM_ERROR_NO_DATA;
	else 
	{
		m_ErrorID = ORM_ERROR_OK;
		for(size_t i=0; i < m_Vars.size(); ++i) 
		{
			const SVarInfo *var = m_Vars.at(i);

			const char *data = result->GetRowData(0, i);

			switch(var->Datatype) 
			{
				case DATATYPE_INT: {
					int int_var = 0;
					if(ConvertStrToInt(data, int_var))
						(*var->Address) = int_var;
					} break;
				case DATATYPE_FLOAT: {
					float float_var = 0.0f;
					if(ConvertStrToFloat(data, float_var))
						(*var->Address) = amx_ftoc(float_var);
					} break;
				case DATATYPE_STRING: 
					amx_SetString(var->Address, data != NULL ? data : "NULL", 0, 0, var->MaxLen);
					break;
			}
		}
	}
}

bool COrm::GenerateUpdateQuery(string &dest)
{
    string key;
    if (!m_KeyVar || m_Vars.empty() || !GetVariableValue(m_KeyVar, key)) return false;
    string query = "UPDATE " + QuoteIdentifier(m_TableName) + " SET ";
    for (size_t i = 0; i < m_Vars.size(); ++i) {
        string value;
        if (!GetVariableValue(m_Vars[i], value)) return false;
        if (i) query += ',';
        query += QuoteIdentifier(m_Vars[i]->Name) + '=' + value;
    }
    query += " WHERE " + QuoteIdentifier(m_KeyVar->Name) + '=' + key + " LIMIT 1";
    dest.swap(query);
    return true;
}

bool COrm::GenerateInsertQuery(string &dest)
{
    if (!m_ConnHandle) return false;
    vector<SVarInfo *> vars(m_Vars);
    // Only integer keys are omitted for auto-increment.
    if (m_KeyVar && m_KeyVar->Datatype == DATATYPE_STRING) vars.push_back(m_KeyVar);
    string fields, values;
    for (size_t i = 0; i < vars.size(); ++i) {
        string value;
        if (!GetVariableValue(vars[i], value)) return false;
        if (i) { fields += ','; values += ','; }
        fields += QuoteIdentifier(vars[i]->Name);
        values += value;
    }
    dest = "INSERT INTO " + QuoteIdentifier(m_TableName) +
        " (" + fields + ") VALUES (" + values + ')';
    return true;
}

void COrm::ApplyInsertResult(CMySQLResult *result) 
{	
	if(result == NULL)
		m_ErrorID = ORM_ERROR_NO_DATA;
	else 
	{
		m_ErrorID = ORM_ERROR_OK;
		if(m_KeyVar != NULL && m_KeyVar->Datatype == DATATYPE_INT && result->InsertID() != 0)
		{
			if (result->InsertID() > static_cast<my_ulonglong>((std::numeric_limits<cell>::max)())) {
				m_ErrorID = ORM_ERROR_NO_DATA;
				CLog::Get()->LogFunction(LOG_ERROR, "COrm::ApplyInsertResult", "insert id exceeds Pawn cell range");
				return;
			}
			(*(m_KeyVar->Address)) = (cell)result->InsertID();
		}
	}
}

bool COrm::GenerateDeleteQuery(string &dest)
{
    string key;
    if (!m_KeyVar || !GetVariableValue(m_KeyVar, key)) return false;
    dest = "DELETE FROM " + QuoteIdentifier(m_TableName) + " WHERE " +
        QuoteIdentifier(m_KeyVar->Name) + '=' + key + " LIMIT 1";
    return true;
}

unsigned short COrm::GenerateSaveQuery(string &dest)
{
    if (!m_ConnHandle || !m_KeyVar) return ORM_QUERYTYPE_INVALID;
    const bool hasKey = m_KeyVar->Datatype == DATATYPE_STRING ?
        *m_KeyVar->Address != 0 : *m_KeyVar->Address > 0;
    if (hasKey)
        return GenerateUpdateQuery(dest) ? ORM_QUERYTYPE_UPDATE : ORM_QUERYTYPE_INVALID;
    return GenerateInsertQuery(dest) ? ORM_QUERYTYPE_INSERT : ORM_QUERYTYPE_INVALID;
}

bool COrm::AddVariable(const char *varname, cell *address, unsigned short datatype, size_t len) 
{
	if(varname == NULL || !*varname || address == NULL ||
	   (datatype != DATATYPE_INT && datatype != DATATYPE_FLOAT && datatype != DATATYPE_STRING) ||
	   (datatype == DATATYPE_STRING && len == 0))
	{
		CLog::Get()->LogFunction(LOG_ERROR, "COrm::AddVariable", "invalid variable name or address");
		return false;
	}

	//abort variable saving if there is already one with same name
	if (m_KeyVar != NULL && m_KeyVar->Name == varname) return false;
	for(vector<SVarInfo *>::iterator v = m_Vars.begin(), end = m_Vars.end(); v != end; ++v)
	{
		if((*v)->Name.compare(varname) == 0)
		{
			CLog::Get()->LogFunction(LOG_ERROR, "COrm::AddVariable", "variable has already been saved");
			return false;
		}
	}
	
	m_Vars.push_back( new SVarInfo(varname, address, datatype, len) );
	m_Lifetime = std::make_shared<int>(0);
	return true;
}

bool COrm::RemoveVariable(const char *varname)
{
	if (varname == NULL) return false;
	if(m_KeyVar != NULL)
	{
		if(m_KeyVar->Name.compare(varname) == 0)
		{
			delete m_KeyVar;
			m_KeyVar = NULL;
			m_Lifetime = std::make_shared<int>(0);
			return true;
		}
	}
	
	for(vector<SVarInfo *>::iterator v = m_Vars.begin(), end = m_Vars.end(); v != end; ++v)
	{
		if((*v)->Name.compare(varname) == 0)
		{
			delete (*v);
			m_Vars.erase(v);
			m_Lifetime = std::make_shared<int>(0);
			return true;
		}
	}
	return false;
}

bool COrm::SetVariableAsKey(const char *varname) 
{
	if (varname == NULL) return false;
	if(m_KeyVar != NULL && m_KeyVar->Name.compare(varname) == 0)
	{
		CLog::Get()->LogFunction(LOG_ERROR, "COrm::SetVariableAsKey", "variable is already set as key");
		return false;
	}

	//set new key
	for(size_t i=0; i < m_Vars.size(); ++i) 
	{
		SVarInfo *key_var = m_Vars.at(i);
		if(key_var->Name.compare(varname) == 0) 
		{
			if (key_var->Datatype != DATATYPE_INT && key_var->Datatype != DATATYPE_STRING) {
				CLog::Get()->LogFunction(LOG_ERROR, "COrm::SetVariableAsKey", "key must be integer or string");
				return false;
			}
			m_Vars.erase(m_Vars.begin()+i);
			if(m_KeyVar != NULL)  //move key if there is one
				m_Vars.push_back(m_KeyVar);
			m_KeyVar = key_var;
			m_Lifetime = std::make_shared<int>(0);
			return true;
		}
	}

	CLog::Get()->LogFunction(LOG_ERROR, "COrm::SetVariableAsKey", "variable not found");
	return false;
}

COrm::~COrm() 
{
	for(vector<SVarInfo *>::iterator v = m_Vars.begin(), end = m_Vars.end(); v != end; ++v)
		delete (*v);
	
	if(m_KeyVar != NULL)
		delete m_KeyVar;
}

void COrm::ClearVariableValues() 
{
	for(vector<SVarInfo *>::iterator v = m_Vars.begin(), end = m_Vars.end(); v != end; ++v) 
	{
		switch( (*v)->Datatype) 
		{
			case DATATYPE_INT:
				(*((*v)->Address)) = 0;
				break;
			case DATATYPE_FLOAT: {
				float EmtpyFloat = 0.0f;
				(*((*v)->Address)) = amx_ftoc(EmtpyFloat);
				} break;
			case DATATYPE_STRING:
				amx_SetString((*v)->Address, "", 0, 0, (*v)->MaxLen);
				break;
		}
	}
	//also clear key variable
	if(m_KeyVar != NULL)
	{
		if(m_KeyVar->Datatype == DATATYPE_STRING)
			amx_SetString(m_KeyVar->Address, "", 0, 0, m_KeyVar->MaxLen);
		else //DATATYPE_INT
			(*(m_KeyVar->Address)) = 0;
	}
}
