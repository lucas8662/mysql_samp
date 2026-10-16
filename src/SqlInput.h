#pragma once

#include <cerrno>
#include <cstdlib>
#include <cctype>
#include <istream>
#include <iterator>
#include <string>
#include <vector>

namespace SqlInput
{
inline std::string Trim(const std::string& value)
{
    const size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

inline bool ParseUnsigned(const std::string& value, size_t& output)
{
    const std::string text = Trim(value);
    if (text.empty()) return false;
    for (unsigned char c : text)
        if (c < '0' || c > '9') return false;
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(text.c_str(), &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed > static_cast<size_t>(-1)) return false;
    output = static_cast<size_t>(parsed);
    return true;
}

// Standard MySQL quoting, including backslash and doubled quote escapes.
// DELIMITER directives are deliberately rejected: they are client commands.
inline bool ParseQueries(std::istream& input, std::vector<std::string>& queries)
{
    queries.clear();
    std::string sql((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) return false;
    std::string current;
    char quote = 0;
    bool lineComment = false, blockComment = false, preserveComment = false;
    for (size_t i = 0; i < sql.size(); ++i)
    {
        const char c = sql[i];
        const char next = i + 1 < sql.size() ? sql[i + 1] : '\0';
        if (lineComment)
        {
            if (c == '\n') { lineComment = false; current += '\n'; }
            continue;
        }
        if (blockComment)
        {
            if (preserveComment) current += c;
            if (c == '*' && next == '/')
            {
                blockComment = false; ++i;
                if (preserveComment) current += '/';
                else current += ' ';
            }
            continue;
        }
        if (quote)
        {
            current += c;
            if (quote != '`' && c == '\\' && next) { current += next; ++i; }
            else if (c == quote)
            {
                if (next == quote) { current += next; ++i; }
                else quote = 0;
            }
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') { quote = c; current += c; }
        else if (c == '#' || (c == '-' && next == '-' &&
            (i + 2 == sql.size() || std::isspace(static_cast<unsigned char>(sql[i + 2])))))
        { lineComment = true; current += ' '; }
        else if (c == '/' && next == '*')
        {
            // Keep executable comments and optimizer hints intact.
            preserveComment = i + 2 < sql.size() && (sql[i + 2] == '!' || sql[i + 2] == '+' ||
                (sql[i + 2] == 'M' && i + 3 < sql.size() && sql[i + 3] == '!'));
            blockComment = true; ++i;
            current += preserveComment ? "/*" : " ";
        }
        else if (c == ';')
        {
            if (!Trim(current).empty()) queries.push_back(Trim(current) + ';');
            current.clear();
        }
        else current += c;
    }
    if (quote || blockComment) { queries.clear(); return false; }
    if (!Trim(current).empty()) queries.push_back(Trim(current));
    for (const auto& query : queries)
    {
        std::string command = query.substr(0, 9);
        for (char& c : command) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (command == "DELIMITER" && (query.size() == 9 || std::isspace(static_cast<unsigned char>(query[9]))))
        { queries.clear(); return false; }
    }
    return true;
}
}
