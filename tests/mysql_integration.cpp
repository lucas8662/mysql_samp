// End-to-end test of the shipped plugin binary against a real MySQL/MariaDB
// server. The binary is loaded with dlopen() exactly like the SA-MP/open.mp
// server does, and a minimal fake AMX drives the natives and receives the
// callbacks through ProcessTick().
//
// Usage: mysql_integration ./bin/mysql.so   (or: ctest -L integration)
// Server comes from MYSQL_TEST_HOST, MYSQL_TEST_PORT, MYSQL_TEST_USER,
// MYSQL_TEST_PASSWORD and MYSQL_TEST_DATABASE; without MYSQL_TEST_HOST the
// test exits with 77, which ctest reports as skipped.
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <dlfcn.h>
#include "sdk/amx/amx.h"
#include "sdk/plugincommon.h"

using std::string;
using std::vector;

static int failures;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define REQUIRE(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: REQUIRE failed: %s\n", __FILE__, __LINE__, #cond); std::exit(1); } } while (0)

// ---------------------------------------------------------------------------
// Fake AMX: one flat cell heap addressed by byte offset, like a real data
// segment. Address 0 is reserved so that it never looks like a valid string.
// ---------------------------------------------------------------------------
static vector<cell> heap(1 << 21);
static size_t heapTop = 1;

static cell Alloc(size_t cells)
{
    REQUIRE(heapTop + cells < heap.size());
    const cell addr = static_cast<cell>(heapTop * sizeof(cell));
    heapTop += cells;
    return addr;
}
static cell *Phys(cell addr) { return &heap[addr / sizeof(cell)]; }
static cell Str(const string &s)
{
    const cell addr = Alloc(s.size() + 1);
    cell *p = Phys(addr);
    for (size_t i = 0; i != s.size(); ++i) p[i] = static_cast<unsigned char>(s[i]);
    p[s.size()] = 0;
    return addr;
}
static string ReadStr(cell addr)
{
    string out;
    for (const cell *p = Phys(addr); *p; ++p) out += static_cast<char>(*p);
    return out;
}
static cell Ref(cell value) { const cell addr = Alloc(1); *Phys(addr) = value; return addr; }
static cell FloatBits(float f) { cell c; std::memcpy(&c, &f, sizeof c); return c; }
static float BitsFloat(cell c) { float f; std::memcpy(&f, &c, sizeof f); return f; }

static int AMXAPI FakeGetAddr(AMX *, cell addr, cell **phys) { *phys = Phys(addr); return AMX_ERR_NONE; }
static int AMXAPI FakeStrLen(const cell *s, int *len) { int n = 0; while (s[n]) ++n; *len = n; return AMX_ERR_NONE; }
static int AMXAPI FakeGetString(char *dest, const cell *src, int, size_t size)
{
    size_t i = 0;
    while (i + 1 < size && src[i]) { dest[i] = static_cast<char>(src[i]); ++i; }
    if (size) dest[i] = 0;
    return AMX_ERR_NONE;
}
static int AMXAPI FakeSetString(cell *dest, const char *src, int, int, size_t size)
{
    size_t i = 0;
    while (i + 1 < size && src[i]) { dest[i] = static_cast<unsigned char>(src[i]); ++i; }
    if (size) dest[i] = 0;
    return AMX_ERR_NONE;
}

// Callback dispatch. Pushed values arrive last argument first.
typedef std::function<void(const vector<cell> &)> Handler;
static vector<string> publicNames;
static vector<Handler> publicHandlers;
static vector<cell> pushed;
static std::map<string, AMX_NATIVE> natives;

static void Public(const string &name, Handler handler)
{
    publicNames.push_back(name);
    publicHandlers.push_back(handler);
}
static int AMXAPI FakeFindPublic(AMX *, const char *name, int *index)
{
    for (size_t i = 0; i != publicNames.size(); ++i)
        if (publicNames[i] == name) { *index = static_cast<int>(i); return AMX_ERR_NONE; }
    return AMX_ERR_NOTFOUND;
}
static int AMXAPI FakePush(AMX *, cell value) { pushed.push_back(value); return AMX_ERR_NONE; }
static int AMXAPI FakePushString(AMX *, cell *amx_addr, cell **phys, const char *s, int, int)
{
    const cell addr = Str(s);
    if (amx_addr) *amx_addr = addr;
    if (phys) *phys = Phys(addr);
    pushed.push_back(addr);
    return AMX_ERR_NONE;
}
static int AMXAPI FakePushArray(AMX *, cell *amx_addr, cell **phys, const cell array[], int numcells)
{
    const cell addr = Alloc(numcells);
    std::memcpy(Phys(addr), array, numcells * sizeof(cell));
    if (amx_addr) *amx_addr = addr;
    if (phys) *phys = Phys(addr);
    pushed.push_back(addr);
    return AMX_ERR_NONE;
}
static int AMXAPI FakeRelease(AMX *, cell addr) { heapTop = addr / sizeof(cell); return AMX_ERR_NONE; }
static int AMXAPI FakeExec(AMX *, cell *retval, int index)
{
    vector<cell> args(pushed.rbegin(), pushed.rend());
    pushed.clear();
    publicHandlers.at(index)(args);
    if (retval) *retval = 1;
    return AMX_ERR_NONE;
}
static int AMXAPI FakeRegister(AMX *, const AMX_NATIVE_INFO *list, int number)
{
    for (int i = 0; (number < 0 || i < number) && list[i].name != NULL; ++i)
        natives[list[i].name] = list[i].func;
    return AMX_ERR_NONE;
}
static void FakeLogprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::putchar('\n');
}

static AMX amx;

// Calls a native the way the AMX does: params[0] holds the argument byte count.
static cell Call(const char *name, std::initializer_list<cell> args)
{
    auto it = natives.find(name);
    REQUIRE(it != natives.end());
    vector<cell> params;
    params.push_back(static_cast<cell>(args.size() * sizeof(cell)));
    params.insert(params.end(), args.begin(), args.end());
    return it->second(&amx, params.data());
}

static void (PLUGIN_CALL *processTick)();

// Runs server ticks until the predicate holds, failing after the timeout.
static bool TickUntil(std::function<bool()> done, int timeoutMs = 20000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!done())
    {
        if (std::chrono::steady_clock::now() > deadline) return false;
        processTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

static string Env(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value != NULL && *value ? value : fallback;
}

int main(int argc, char **argv)
{
    if (argc != 2) { std::fprintf(stderr, "usage: %s <plugin.so>\n", argv[0]); return 2; }
    if (std::getenv("MYSQL_TEST_HOST") == NULL)
    {
        std::puts("MYSQL_TEST_HOST is not set; skipping live database tests");
        return 77; // ctest SKIP_RETURN_CODE
    }
    void *plugin = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!plugin) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }

    typedef bool (PLUGIN_CALL *Load_t)(void **);
    typedef void (PLUGIN_CALL *Unload_t)();
    typedef int (PLUGIN_CALL *AmxLoad_t)(AMX *);
    Load_t load = reinterpret_cast<Load_t>(dlsym(plugin, "Load"));
    Unload_t unload = reinterpret_cast<Unload_t>(dlsym(plugin, "Unload"));
    AmxLoad_t amxLoad = reinterpret_cast<AmxLoad_t>(dlsym(plugin, "AmxLoad"));
    AmxLoad_t amxUnload = reinterpret_cast<AmxLoad_t>(dlsym(plugin, "AmxUnload"));
    processTick = reinterpret_cast<void (PLUGIN_CALL *)()>(dlsym(plugin, "ProcessTick"));
    REQUIRE(load && unload && amxLoad && amxUnload && processTick);

    static void *exports[64] = {};
    exports[PLUGIN_AMX_EXPORT_GetAddr] = reinterpret_cast<void *>(&FakeGetAddr);
    exports[PLUGIN_AMX_EXPORT_StrLen] = reinterpret_cast<void *>(&FakeStrLen);
    exports[PLUGIN_AMX_EXPORT_GetString] = reinterpret_cast<void *>(&FakeGetString);
    exports[PLUGIN_AMX_EXPORT_SetString] = reinterpret_cast<void *>(&FakeSetString);
    exports[PLUGIN_AMX_EXPORT_FindPublic] = reinterpret_cast<void *>(&FakeFindPublic);
    exports[PLUGIN_AMX_EXPORT_Push] = reinterpret_cast<void *>(&FakePush);
    exports[PLUGIN_AMX_EXPORT_PushString] = reinterpret_cast<void *>(&FakePushString);
    exports[PLUGIN_AMX_EXPORT_PushArray] = reinterpret_cast<void *>(&FakePushArray);
    exports[PLUGIN_AMX_EXPORT_Release] = reinterpret_cast<void *>(&FakeRelease);
    exports[PLUGIN_AMX_EXPORT_Exec] = reinterpret_cast<void *>(&FakeExec);
    exports[PLUGIN_AMX_EXPORT_Register] = reinterpret_cast<void *>(&FakeRegister);
    static void *data[256] = {};
    data[PLUGIN_DATA_LOGPRINTF] = reinterpret_cast<void *>(&FakeLogprintf);
    data[PLUGIN_DATA_AMX_EXPORTS] = exports;

    REQUIRE(load(data));
    REQUIRE(amxLoad(&amx) == AMX_ERR_NONE);
    Call("mysql_log", { 1 | 2, 1 }); // LOG_ERROR | LOG_WARNING, text log

    // --- connect -----------------------------------------------------------
    const string host = Env("MYSQL_TEST_HOST", "127.0.0.1");
    const int port = std::atoi(Env("MYSQL_TEST_PORT", "3306").c_str());
    const cell h = Call("mysql_connect", { Str(host), Str(Env("MYSQL_TEST_USER", "root")),
        Str(Env("MYSQL_TEST_DATABASE", "samp_test")), Str(Env("MYSQL_TEST_PASSWORD", "")),
        port, 1, 2 });
    REQUIRE(h > 0);
    if (Call("mysql_errno", { h }) != 0)
    {
        const cell buf = Alloc(256);
        Call("mysql_error", { buf, h, 256 });
        std::fprintf(stderr, "connection failed: %s\n", ReadStr(buf).c_str());
        return 1;
    }
    const cell buf = Alloc(512);
    Call("mysql_get_charset", { buf, h, 512 });
    std::printf("connected to %s:%d, charset %s\n", host.c_str(), port, ReadStr(buf).c_str());

    // --- unthreaded queries -----------------------------------------------
    Call("mysql_query", { h, Str("DROP TABLE IF EXISTS it_test"), 0 });
    Call("mysql_query", { h, Str("CREATE TABLE it_test (id INT AUTO_INCREMENT PRIMARY KEY, "
        "name VARCHAR(64) NOT NULL, score FLOAT NOT NULL) ENGINE=InnoDB"), 0 });
    CHECK(Call("mysql_errno", { h }) == 0);

    // --- mysql_format: %e escaping, %d and %f ------------------------------
    Call("mysql_format", { h, buf, 512, Str("INSERT INTO it_test (name, score) VALUES ('%e', %f) /* %d */"),
        Str("O'Brien"), Ref(FloatBits(1.5f)), Ref(7) });
    const string insert = ReadStr(buf);
    CHECK(insert.find("'O\\'Brien'") != string::npos);
    CHECK(insert.find("/* 7 */") != string::npos);

    // --- threaded query with callback arguments ----------------------------
    int insertCalls = 0;
    Public("OnInsert", [&](const vector<cell> &a) {
        ++insertCalls;
        CHECK(a.size() == 2 && a[0] == 42 && ReadStr(a[1]) == "tag");
        CHECK(Call("cache_insert_id", { h }) == 1);
        CHECK(Call("cache_affected_rows", { h }) == 1);
    });
    CHECK(Call("mysql_tquery", { h, Str(insert), Str("OnInsert"), Str("ds"), Ref(42), Str("tag") }) == 1);
    CHECK(TickUntil([&] { return insertCalls == 1; }));

    int selectCalls = 0;
    Public("OnSelect", [&](const vector<cell> &) {
        ++selectCalls;
        CHECK(Call("cache_get_row_count", { h }) == 1);
        CHECK(Call("cache_get_field_count", { h }) == 3);
        const cell dest = Alloc(64);
        Call("cache_get_field_content", { 0, Str("name"), dest, h, 64 });
        CHECK(ReadStr(dest) == "O'Brien");
        CHECK(Call("cache_get_row_int", { 0, 0, h }) == 1);
        CHECK(std::fabs(BitsFloat(Call("cache_get_row_float", { 0, 2, h })) - 1.5f) < 1e-6f);
    });
    CHECK(Call("mysql_tquery", { h, Str("SELECT id, name, score FROM it_test"), Str("OnSelect"), Str("") }) == 1);
    CHECK(TickUntil([&] { return selectCalls == 1; }));

    // --- errors reach OnQueryError ----------------------------------------
    int errorCalls = 0;
    Public("OnQueryError", [&](const vector<cell> &a) {
        ++errorCalls;
        CHECK(a.size() == 5 && a[0] == 1146); // ER_NO_SUCH_TABLE
        CHECK(ReadStr(a[2]) == "OnSelect");
        CHECK(a[4] == h);
    });
    Call("mysql_tquery", { h, Str("SELECT * FROM it_missing_table"), Str("OnSelect"), Str("") });
    CHECK(TickUntil([&] { return errorCalls == 1; }));
    CHECK(selectCalls == 1);

    // --- parallel queries over the connection pool -------------------------
    int poolCalls = 0;
    Public("OnPool", [&](const vector<cell> &) { ++poolCalls; });
    for (int i = 0; i != 20; ++i)
        CHECK(Call("mysql_pquery", { h, Str("INSERT INTO it_test (name, score) VALUES ('pool', 0)"), Str("OnPool"), Str("") }) == 1);
    CHECK(TickUntil([&] { return poolCalls == 20; }));

    // --- prepared statements ----------------------------------------------
    int stmtCalls = 0;
    Public("OnStmt", [&](const vector<cell> &) {
        ++stmtCalls;
        CHECK(Call("cache_get_row_count", { h }) == 1);
        const cell dest = Alloc(64);
        Call("cache_get_row", { 0, 0, dest, h, 64 });
        CHECK(ReadStr(dest) == "O'Brien");
    });
    const cell stmt = Call("mysql_stmt_prepare", { h, Str("SELECT name FROM it_test WHERE id = ? AND name = ?") });
    CHECK(stmt > 0);
    CHECK(Call("mysql_stmt_bind_int", { stmt, 0, 1 }) == 1);
    CHECK(Call("mysql_stmt_bind_string", { stmt, 1, Str("O'Brien") }) == 1);
    CHECK(Call("mysql_stmt_execute", { stmt, Str("OnStmt"), Str("") }) == 1);
    CHECK(TickUntil([&] { return stmtCalls == 1; }));
    Call("mysql_stmt_close", { stmt });

    // --- transactions: commit and rollback ---------------------------------
    int txCalls = 0;
    Public("OnTx", [&](const vector<cell> &) { ++txCalls; });
    cell tx = Call("mysql_transaction_begin", { h });
    CHECK(tx > 0);
    Call("mysql_transaction_query", { tx, Str("INSERT INTO it_test (name, score) VALUES ('tx', 1)") });
    Call("mysql_transaction_query", { tx, Str("INSERT INTO it_test (name, score) VALUES ('tx', 2)") });
    CHECK(Call("mysql_transaction_commit", { tx, Str("OnTx"), Str("") }) == 1);
    CHECK(TickUntil([&] { return txCalls == 1; }));

    tx = Call("mysql_transaction_begin", { h });
    Call("mysql_transaction_query", { tx, Str("INSERT INTO it_test (name, score) VALUES ('rolled back', 0)") });
    Call("mysql_transaction_rollback", { tx });

    auto Count = [&]() {
        const cell cache = Call("mysql_query", { h, Str("SELECT COUNT(*) FROM it_test"), 1 });
        const cell n = Call("cache_get_row_int", { 0, 0, h });
        Call("cache_delete", { cache, h });
        return n;
    };
    CHECK(Count() == 1 + 20 + 2);

    // --- reconnect keeps the handle usable --------------------------------
    CHECK(Call("mysql_reconnect", { h }) == 1);
    int afterReconnect = 0;
    Public("OnAfterReconnect", [&](const vector<cell> &) { ++afterReconnect; });
    Call("mysql_tquery", { h, Str("SELECT 1"), Str("OnAfterReconnect"), Str("") });
    CHECK(TickUntil([&] { return afterReconnect == 1; }));

    // --- no query or callback is dropped under load ------------------------
    // More queries than the old query queue (16876) and more results than the
    // old callback queue (32648) could hold, all before a single ProcessTick.
    const int burst = 35000;
    int burstCalls = 0;
    Public("OnBurst", [&](const vector<cell> &) { ++burstCalls; });
    const cell burstQuery = Str("DO 0"), burstName = Str("OnBurst"), noFormat = Str("");
    for (int i = 0; i != burst; ++i)
        REQUIRE(Call("mysql_tquery", { h, burstQuery, burstName, noFormat }) == 1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (Call("mysql_unprocessed_queries", { h }) != 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(Call("mysql_unprocessed_queries", { h }) == 0);
    processTick();
    CHECK(burstCalls == burst);

    // --- cleanup -------------------------------------------------------------
    Call("mysql_query", { h, Str("DROP TABLE it_test"), 0 });
    CHECK(Call("mysql_close", { h }) == 1);
    amxUnload(&amx);
    unload();
    dlclose(plugin);

    if (failures)
    {
        std::fprintf(stderr, "MySQL integration: %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("MySQL integration passed: connect, format, tquery/pquery, cache, errors, stmt, transactions, reconnect, callback burst");
    return 0;
}
