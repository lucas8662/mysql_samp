#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>
#include <locale>
#include <sstream>
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

// Test-only fixtures: no live database and no worker threads are created.
#define private public
#include "CMySQLConnection.h"
#include "CMySQLHandle.h"
#include "CMySQLResult.h"
#undef private
#include "CMySQLQuery.h"
#include "COrm.h"
#include "CCallback.h"

extern void *pAMXFunctions;
static cell FloatCell(float value)
{
    cell result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
static int AMXAPI ReadString(char *dest, const cell *src, int, size_t size)
{
    size_t i = 0;
    while (i + 1 < size && src[i]) { dest[i] = static_cast<char>(src[i]); ++i; }
    if (size) dest[i] = 0;
    return AMX_ERR_NONE;
}
static int AMXAPI WriteString(cell *dest, const char *src, int, int, size_t size)
{
    size_t i = 0;
    while (i + 1 < size && src[i]) { dest[i] = static_cast<unsigned char>(src[i]); ++i; }
    if (size) dest[i] = 0;
    return AMX_ERR_NONE;
}
static void Bind(CMySQLQuery *query, COrm *orm)
{
    query->Orm.Object = orm;
    query->Orm.Lifetime = orm->GetLifetime();
    query->Orm.Bound = true;
    query->Orm.Type = ORM_QUERYTYPE_SELECT;
}

int main()
{
    void *exports[64]{};
    exports[PLUGIN_AMX_EXPORT_GetString] = reinterpret_cast<void *>(&ReadString);
    exports[PLUGIN_AMX_EXPORT_SetString] = reinterpret_cast<void *>(&WriteString);
    pAMXFunctions = exports;
    CLog::Get()->SetLogLevel(LOG_NONE);
    assert(mysql_library_init(0, NULL, NULL) == 0);
    CMySQLHandle *handle = new CMySQLHandle(0);
    string host, user, password, database;
    handle->m_MainConnection = CMySQLConnection::Create(host, user, password, database, 3306, false, false);
    auto *connection = handle->m_MainConnection;
    connection->m_Connection = mysql_init(NULL);
    assert(connection->m_Connection);
    connection->m_IsConnected = true; // EscapeString works with an initialized client charset.

    cell key = 9, floating = FloatCell(1.25f);
    vector<cell> text(7001, 'x');
    text[7000] = 0;
    unsigned id = COrm::Create("tab`le", handle);
    COrm *orm = COrm::GetOrm(id);
    assert(orm->AddVariable("id", &key, DATATYPE_INT));
    assert(orm->SetVariableAsKey("id"));
    assert(!orm->AddVariable("id", &key, DATATYPE_INT));
    assert(orm->AddVariable("te`xt", text.data(), DATATYPE_STRING, text.size()));
    assert(orm->AddVariable("float", &floating, DATATYPE_FLOAT));
    assert(!orm->SetVariableAsKey("float"));
    string query = "previous contents";
    assert(orm->GenerateUpdateQuery(query));
    assert(query.size() > 7000 && query.find("`tab``le`") != string::npos);
    assert(query.find("`te``xt`") != string::npos && query.find("'9'") == string::npos);
    assert(orm->GenerateSelectQuery(query) && query.find("previous contents") == string::npos);
    assert(orm->GenerateInsertQuery(query));
    floating = FloatCell(std::numeric_limits<float>::infinity());
    assert(!orm->GenerateUpdateQuery(query));
    floating = FloatCell(1.25f);
    CMySQLResult result;
    result.m_InsertID = 42;
    orm->ApplyInsertResult(&result);
    assert(key == 42 && orm->GetErrorID() == ORM_ERROR_OK);
    result.m_InsertID = static_cast<my_ulonglong>(std::numeric_limits<cell>::max()) + 1;
    orm->ApplyInsertResult(&result);
    assert(key == 42 && orm->GetErrorID() == ORM_ERROR_NO_DATA);

    CMySQLQuery *pending = new CMySQLQuery;
    Bind(pending, orm);
    orm->Destroy();
    assert(pending->Orm.Cancelled());
    unsigned replacement = COrm::Create("new_table", handle);
    assert(replacement == id && pending->Orm.Cancelled());
    assert(CCallback::Get()->QueueQuery(pending));
    CCallback::Get()->ProcessCallbacks(); // Executes the production cancellation guard.
    COrm::GetOrm(replacement)->Destroy();

    vector<cell> stringKey(2001, 'k');
    stringKey[1999] = '\'';
    stringKey[2000] = 0;
    id = COrm::Create("users", handle);
    orm = COrm::GetOrm(id);
    assert(orm->AddVariable("name", stringKey.data(), DATATYPE_STRING, stringKey.size()));
    assert(orm->SetVariableAsKey("name"));
    assert(orm->GenerateDeleteQuery(query) && query.size() > 2000);
    assert(query.find("\\'") != string::npos);
    assert(orm->GenerateInsertQuery(query) && query.find("(`name`)") != string::npos);
    result.m_InsertID = 123;
    orm->ApplyInsertResult(&result);
    assert(stringKey[0] == 'k' && stringKey[1999] == '\'');
    result.m_InsertID = 0;
    orm->ApplyInsertResult(&result);
    assert(orm->GetErrorID() == ORM_ERROR_OK);
    assert(!orm->GenerateUpdateQuery(query));
    assert(orm->GenerateSaveQuery(query) == ORM_QUERYTYPE_INVALID);
    pending = new CMySQLQuery;
    Bind(pending, orm);
    assert(orm->AddVariable("score", &key, DATATYPE_INT));
    assert(pending->Orm.Cancelled()); // Changed field bindings cannot consume old results.
    delete pending;
    connection->m_IsConnected = false;
    assert(!orm->GenerateDeleteQuery(query));
    connection->m_IsConnected = true;
    pending = new CMySQLQuery;
    Bind(pending, orm);
    COrm::ClearByHandle(handle);
    assert(!COrm::IsValid(id) && pending->Orm.Cancelled());
    pending->Orm.Object = NULL; // SQL errors also retain lifetime cancellation.
    pending->Orm.Type = 0;
    assert(pending->Orm.Cancelled());
    delete pending;

    AMX owner{};
    id = COrm::Create("owned", handle, &owner);
    COrm::ClearByAmx(&owner);
    assert(!COrm::IsValid(id));
    id = COrm::Create("closing", handle);
    handle->Destroy();
    assert(!COrm::IsValid(id));
    mysql_library_end();
    CLog::Destroy();
    puts("ORM regression tests passed (production plugin; mocked Pawn string functions)");
}
