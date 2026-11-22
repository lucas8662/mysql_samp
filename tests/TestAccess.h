#pragma once
// White-box access for the regression tests. Production classes only declare
// "friend struct TestAccess"; nothing here is compiled into the plugin.
#include <atomic>
#include "CMySQLConnection.h"
#include "CMySQLHandle.h"
#include "CMySQLResult.h"

struct TestAccess
{
    static CMySQLHandle *NewHandle(unsigned int id) { return new CMySQLHandle(id); }
    static void RegisterHandle(CMySQLHandle *handle) { CMySQLHandle::SQLHandle[handle->GetID()] = handle; }
    static CMySQLConnection *&MainConnection(CMySQLHandle *handle) { return handle->m_MainConnection; }
    static std::atomic<unsigned int> &QueryCounter(CMySQLHandle *handle) { return handle->m_QueryCounter; }

    static size_t &Port(CMySQLConnection *connection) { return connection->m_Port; }
    static MYSQL *&Mysql(CMySQLConnection *connection) { return connection->m_Connection; }
    static bool &Connected(CMySQLConnection *connection) { return connection->m_IsConnected; }

    static my_ulonglong &InsertId(CMySQLResult &result) { return result.m_InsertID; }
};
