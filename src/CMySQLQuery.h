#pragma once
#ifndef INC_CMYSQLQUERY_H
#define INC_CMYSQLQUERY_H


#include <string>
#include <stack>
#include <vector>
#include <boost/variant.hpp>

using std::string;
using std::stack;
using std::vector;

#include "main.h"
#include "CMySQLConnection.h"


class CMySQLHandle;
class CMySQLResult;
class COrm;


class CMySQLQuery
{
private:
	bool StoreResult(MYSQL *mysql_connection, MYSQL_RES *mysql_result);
	bool ExecutePrepared(MYSQL *mysql_connection);
	bool ExecuteTransaction(MYSQL *mysql_connection);
	bool StorePreparedResult(MYSQL_STMT *statement);

public:
	bool Execute(MYSQL *mysql_connection);


	string Query;

	CMySQLHandle *Handle;
	CMySQLResult *Result;

	bool Unthreaded;

	// Parameters are copied into the queued query so Pawn memory is never used
	// from a connector worker thread.
	struct s_StatementParameter
	{
		enum e_Type { TYPE_INTEGER, TYPE_FLOAT, TYPE_STRING };
		s_StatementParameter() : Type(TYPE_INTEGER), Integer(0), Float(0.0f) {}
		e_Type Type;
		int Integer;
		float Float;
		string String;
	};
	bool IsPreparedStatement;
	vector<s_StatementParameter> StatementParameters;

	bool IsTransaction;
	vector<string> TransactionQueries;

	struct s_Callback
	{
		stack< boost::variant<cell, string> > Params;
		string Name;
	} Callback;

	struct s_Orm
	{
		s_Orm() :
			Object(NULL),
			Type(0)
		{}
		
		COrm *Object;
		unsigned short Type;
	} Orm;


	CMySQLQuery() :
		Handle(NULL),
		Result(NULL),

		Unthreaded(false),
		IsPreparedStatement(false),
		IsTransaction(false)
	{}
	~CMySQLQuery();
	
};


#endif // INC_CMYSQLQUERY_H
