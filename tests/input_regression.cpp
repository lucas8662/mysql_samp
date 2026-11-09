#include "SqlInput.h"
#include "IdAllocator.h"
#include <map>
#include <cassert>
#include <sstream>
#include <iostream>

static std::vector<std::string> Parse(const std::string& sql)
{
    std::istringstream stream(sql);
    std::vector<std::string> queries;
    assert(SqlInput::ParseQueries(stream, queries));
    return queries;
}
int main()
{
    auto queries = Parse("-- heading\nINSERT INTO t VALUES ('a;b', '#x', '-- text'); # tail\nSELECT `semi;colon` FROM t;");
    assert(queries.size() == 2);
    assert(queries[0] == "INSERT INTO t VALUES ('a;b', '#x', '-- text');");
    assert(queries[1] == "SELECT `semi;colon` FROM t;");
    queries = Parse("SELECT 'it''s;a', 'a\\';b', \"x;#y\"; /* ; */ SELECT 2--3;");
    assert(queries.size() == 2 && queries[1] == "SELECT 2--3;");
    assert(Parse("/* comment */ ; # only comment\n-- tail").empty());
    assert(Parse("SELECT 1").size() == 1);
    assert(Parse("SELECT `a\\`; SELECT 2;").size() == 2);
    for (const char *sql : {"SELECT 'unterminated", "SELECT 1; /* unfinished", "DELIMITER $$\nSELECT 1$$"})
    {
        std::istringstream stream(sql);
        assert(!SqlInput::ParseQueries(stream, queries));
    }
    assert(Parse("/*!40101 SET x=1 */;")[0] == "/*!40101 SET x=1 */;");
    size_t value = 42;
    for (const char *text : {"-1", "+1", "", "1x", "184467440737095516160000"})
    {
        assert(!SqlInput::ParseUnsigned(text, value));
        assert(value == 42);
    }
    assert(SqlInput::ParseUnsigned(" 64 \r", value) && value == 64);
    assert(SqlInput::ParseUnsigned("0", value) && value == 0);

    // Handle/ORM IDs: lowest free, starting at 1.
    std::map<unsigned int, int> ids;
    assert(GetLowestFreeId(ids) == 1);
    ids[1] = ids[2] = ids[4] = 0;
    assert(GetLowestFreeId(ids) == 3);
    ids.erase(1);
    assert(GetLowestFreeId(ids) == 1);

    // Cache/statement IDs: monotonic, skip IDs in use, never 0, wrap positive.
    std::map<unsigned int, int> saved;
    unsigned int next = 1;
    assert(GetNextFreeId(saved, next) == 1);
    saved[1] = saved[2] = saved[3] = 0;
    next = 2;
    assert(GetNextFreeId(saved, next) == 4);
    saved.erase(2);
    assert(GetNextFreeId(saved, next) == 5); // freed ID not reused right away
    next = 0x7FFFFFFF;
    saved[0x7FFFFFFF] = 0;
    assert(GetNextFreeId(saved, next) == 2); // wraps, skips 0 and used 1
    std::cout << "SQL input and ID allocation regressions passed\n";
}
