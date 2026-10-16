#include "SqlInput.h"
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
    std::cout << "SQL input regressions passed\n";
}
