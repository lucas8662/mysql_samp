#include <cassert>
#include <cstring>
#include <cstdio>
#include <memory>
#include <queue>
#include <stack>
#include <set>
#include <vector>
#include <boost/thread.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/unordered_map.hpp>
#include <boost/variant.hpp>
#include <boost/function.hpp>
#include "CLog.h"
#include "main.h"
#define private public
#include "CMySQLHandle.h"
#include "CMySQLConnection.h"
#undef private
#include "CMySQLResult.h"
#include "CMySQLQuery.h"
#include "CCallback.h"
#include "CScripting.h"

extern void *pAMXFunctions;
static AMX *expectedOwner;
static CMySQLHandle *closingHandle;
static int executions;
static int AMXAPI FindPublic(AMX *amx, const char *, int *index)
{
    assert(amx == expectedOwner);
    *index = 0;
    return AMX_ERR_NONE;
}
static int AMXAPI Execute(AMX *amx, cell *, int)
{
    assert(amx == expectedOwner);
    ++executions;
    if (closingHandle)
    {
        auto *handle = closingHandle;
        closingHandle = NULL;
        cell params[] = { sizeof(cell), static_cast<cell>(handle->GetID()) };
        assert(Native::mysql_close(amx, params) == 1);
    }
    return AMX_ERR_NONE;
}

// Interpose connector query calls to reproduce a late stored-procedure error.
static std::vector<std::string> commands;
static int nextResult;
static int nextCalls;
static bool storeFailure;
extern "C" int STDCALL mysql_real_query(MYSQL *, const char *text, unsigned long length)
{ commands.push_back(std::string(text, length)); return 0; }
extern "C" MYSQL_RES * STDCALL mysql_store_result(MYSQL *) { return NULL; }
extern "C" unsigned int STDCALL mysql_field_count(MYSQL *) { return storeFailure ? 1 : 0; }
extern "C" int STDCALL mysql_next_result(MYSQL *) { if (nextResult == 1 && nextCalls++ == 0) return 0; return nextResult; }
extern "C" unsigned int STDCALL mysql_errno(MYSQL *) { return 1234; }
extern "C" const char * STDCALL mysql_error(MYSQL *) { return "late query error"; }
extern "C" my_ulonglong STDCALL mysql_affected_rows(MYSQL *) { return 1; }
extern "C" my_ulonglong STDCALL mysql_insert_id(MYSQL *) { return 0; }
extern "C" unsigned int STDCALL mysql_warning_count(MYSQL *) { return 0; }

extern "C" MYSQL_STMT * STDCALL mysql_stmt_init(MYSQL *) { return NULL; }

static CMySQLQuery *Callback(CMySQLHandle *handle, AMX *owner)
{
    auto *query = new CMySQLQuery;
    query->Handle = handle;
    query->Result = new CMySQLResult;
    query->Callback.Name = "SameCallback";
    CCallback::Get()->BindOwner(query, owner);
    return query;
}
int main()
{
    CLog::Get()->SetLogLevel(LOG_NONE);
    void *exports[64]{};
    exports[PLUGIN_AMX_EXPORT_FindPublic] = reinterpret_cast<void *>(&FindPublic);
    exports[PLUGIN_AMX_EXPORT_Exec] = reinterpret_cast<void *>(&Execute);
    pAMXFunctions = exports;
    auto *callbacks = CCallback::Get();
    AMX first{}, second{};
    callbacks->AddAmx(&first);
    callbacks->AddAmx(&second);
    auto *handle = new CMySQLHandle(77);
    CMySQLHandle::SQLHandle[77] = handle;
    expectedOwner = &second;
    assert(callbacks->QueueQuery(Callback(handle, &second)));
    callbacks->ProcessCallbacks();
    assert(executions == 1);
    assert(callbacks->QueueQuery(Callback(handle, &first)));
    callbacks->EraseAmx(&first);
    callbacks->AddAmx(&first); // Same address, different script generation.
    callbacks->ProcessCallbacks();
    assert(executions == 1);
    closingHandle = handle;
    const auto lifetime = handle->GetLifetime();
    assert(callbacks->QueueQuery(Callback(handle, &second)));
    callbacks->ProcessCallbacks();
    assert(executions == 2 && lifetime.expired());

    std::string host("host"), user("user"), password, database("db");
    auto *a = CMySQLConnection::Create(host, user, password, database, 3306, false, false);
    auto *b = CMySQLConnection::Create(host, user, password, database, 3307, false, false);
    assert(!(*a == *b));
    b->m_Port = 3306;
    assert(*a == *b);
    a->Destroy(); b->Destroy();
    assert(!CMySQLHandle::Create(host, user, password, database, 3306, static_cast<size_t>(-1), false));
    assert(!CMySQLHandle::Create(host, user, password, database, 65536, 0, false));
    assert(!CMySQLHandle::Create(host, user, password, database, 0, 0, false));
    assert(!CMySQLHandle::Create(host, user, password, database, 3306, 65, false));

    handle = new CMySQLHandle(78);
    for (int scenario = 0; scenario != 3; ++scenario)
    {
        commands.clear();
        nextCalls = 0;
        storeFailure = scenario == 1;
        nextResult = scenario == 0 ? 1 : -1;
        CMySQLQuery query;
        query.Handle = handle;
        query.IsTransaction = true;
        query.Callback.Name.assign(4096, 'x');
        query.TransactionQueries.push_back("CALL failing_procedure()");
        query.TransactionQueries.push_back("UPDATE should_not_run SET x=1");
        handle->m_QueryCounter = 1;
        const bool success = query.Execute(reinterpret_cast<MYSQL *>(1));
        if (scenario < 2)
        {
            assert(!success && commands.size() == 3);
            assert(commands[0] == "START TRANSACTION" && commands[2] == "ROLLBACK");
            assert(query.Callback.Name == "OnQueryError");
            assert(boost::get<cell>(query.Callback.Params.top()) == 78);
        }
        else
        {
            assert(success && commands.size() == 4 && commands.back() == "COMMIT");
        }
        assert(handle->GetUnprocessedQueryCount() == 0);
    }
    // Ordinary query logging also accepts callbacks longer than the old buffer.
    {
        CMySQLQuery query;
        query.Handle = handle;
        query.Callback.Name.assign(4096, 'x');
        handle->m_QueryCounter = 1;
        assert(query.Execute(reinterpret_cast<MYSQL *>(1)));
    }
    {
        CMySQLQuery query;
        query.Handle = handle;
        query.IsPreparedStatement = true;
        query.Callback.Name.assign(4096, 'x');
        handle->m_QueryCounter = 1;
        assert(!query.Execute(reinterpret_cast<MYSQL *>(1)));
        assert(query.Callback.Name == "OnQueryError");
    }
    handle->Destroy();
    callbacks->Destroy();
    CLog::Destroy();
    puts("Core regressions passed: callback ownership/lifetime, ports, limits, transactions and long names");
}
