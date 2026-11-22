#pragma once
#ifndef INC_CMYSQLQUERY_H
#define INC_CMYSQLQUERY_H


#include <chrono>
#include <string>
#include <stack>
#include <vector>
#include <memory>
#include <variant>

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

	// Follows one query through queue, execution and callback in the debug
	// log; every line about it carries Tag() ("q#<id>").
	struct s_Trace
	{
		typedef std::chrono::steady_clock clock;
		s_Trace();
		unsigned int Id;
		clock::time_point Queued; //unset for unthreaded queries
		clock::time_point Started;
		clock::time_point Finished;
	} Trace;
	string Tag() const { return "q#" + std::to_string(Trace.Id); }
	// Name for the log's function column, e.g. "CMySQLQuery::Execute[OnLoad, q#12]".
	string LogName(const char *function) const;

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
		s_Callback() : Owner(NULL) {}
		AMX *Owner;
		std::weak_ptr<void> Lifetime;
		stack< std::variant<cell, string> > Params;
		string Name;
	} Callback;

	struct s_Orm
	{
		s_Orm() :
			Object(NULL),
			Bound(false),
			Type(0)
		{}
		
		COrm *Object;
		std::weak_ptr<void> Lifetime;
		bool Bound;
		bool Cancelled() const { return Bound && Lifetime.expired(); }
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
