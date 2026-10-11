module;

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

module orm;

import :migrations_internal;

namespace orm::migrations::detail
{
namespace
{
auto letter(char c) -> bool
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 128;
}
auto identifier(char c) -> bool
{
    return letter(c) || (c >= '0' && c <= '9') || c == '$';
}
auto upper(std::string_view word) -> std::string
{
    std::string result{word};
    for (auto& c : result)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return result;
}
auto executableCommentEnd(std::string_view script, std::size_t position) -> std::size_t
{
    while (position < script.size())
    {
        const auto c = script[position];
        if (c == '\'' || c == '"' || c == '`')
        {
            const auto quote = c;
            ++position;
            while (position < script.size())
            {
                if (script[position] == '\\' && quote != '`')
                {
                    position += 2;
                }
                else if (script[position++] == quote)
                {
                    if (position < script.size() && script[position] == quote)
                    {
                        ++position;
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
        else if (c == '#' || (c == '-' && position + 2 < script.size() && script[position + 1] == '-' &&
                              static_cast<unsigned char>(script[position + 2]) <= 32))
        {
            const auto end = script.find_first_of("\r\n", position);
            position = end == std::string_view::npos ? script.size() : end + 1;
        }
        else if (position + 1 < script.size() && script.substr(position, 2) == "/*")
        {
            // A regular comment inside executable SQL does not terminate its
            // enclosing comment. Nested executable markers are regular comments.
            const auto end = script.find("*/", position + 2);
            if (end == std::string_view::npos)
            {
                return end;
            }
            position = end + 2;
        }
        else if (position + 1 < script.size() && script.substr(position, 2) == "*/")
        {
            return position;
        }
        else
        {
            ++position;
        }
    }
    return std::string_view::npos;
}
} // namespace

auto validateScript(std::string_view script, db::BackendType backend, Version version) -> void
{
    const auto mysql = backend == db::BackendType::Mysql || backend == db::BackendType::Mariadb;
    const auto invalid = [=]
    {
        throw MigrationError{ErrorCode::InvalidSql, "SQL contains unsupported transaction, session or client commands",
                             version, backend};
    };
    // Executable comments are SQL, including version-conditional comments. Validate
    // all their bodies regardless of the server version so a later upgrade cannot
    // turn previously accepted text into transaction/session control.
    std::string expanded;
    if (mysql)
    {
        expanded = std::string{script};
        std::size_t executableEnd = 0;
        for (std::size_t i = 0; i < expanded.size();)
        {
            if (expanded[i] == '\'' || expanded[i] == '"' || expanded[i] == '`')
            {
                const auto quote = expanded[i++];
                while (i < expanded.size())
                {
                    if (expanded[i] == '\\' && quote != '`')
                    {
                        i += 2;
                    }
                    else if (expanded[i++] == quote)
                    {
                        if (i < expanded.size() && expanded[i] == quote)
                        {
                            ++i;
                        }
                        else
                        {
                            break;
                        }
                    }
                }
            }
            else if (expanded[i] == '#' || (expanded[i] == '-' && i + 2 < expanded.size() && expanded[i + 1] == '-' &&
                                            static_cast<unsigned char>(expanded[i + 2]) <= 32))
            {
                const auto end = expanded.find_first_of("\r\n", i);
                i = end == std::string::npos ? expanded.size() : end + 1;
            }
            else if (i + 1 < expanded.size() && expanded.substr(i, 2) == "/*")
            {
                const bool executable = i >= executableEnd && i + 2 < expanded.size() && expanded[i + 2] == '!';
                const bool mariaExecutable =
                    i >= executableEnd && i + 3 < expanded.size() && expanded[i + 2] == 'M' && expanded[i + 3] == '!';
                const auto end = executable || mariaExecutable ?
                                     executableCommentEnd(expanded, i + (executable ? 3 : 4)) :
                                     expanded.find("*/", i + 2);
                if (end == std::string::npos)
                {
                    invalid();
                }
                if (executable || mariaExecutable)
                {
                    auto start = i + (executable ? 3 : 4);
                    while (start < end && expanded[start] >= '0' && expanded[start] <= '9')
                    {
                        ++start;
                    }
                    std::fill(expanded.begin() + static_cast<std::ptrdiff_t>(i),
                              expanded.begin() + static_cast<std::ptrdiff_t>(start), ' ');
                    expanded[end] = expanded[end + 1] = ' ';
                    executableEnd = end + 2;
                    i = start;
                }
                else
                {
                    i = end + 2;
                }
            }
            else
            {
                ++i;
            }
        }
        script = expanded;
    }
    std::vector<std::string> words;
    bool routine = false;
    std::vector<std::string_view> blocks;
    const auto check = [&]
    {
        if ((backend != db::BackendType::Postgres && !mysql) || words.empty())
        {
            return;
        }
        const auto& first = words.front();
        if (std::ranges::find(words, "_ORM_MIGRATIONS") != words.end() ||
            std::ranges::find(words, "_ORM_MIGRATIONS_INCOMPLETE") != words.end())
        {
            invalid();
        }
        if (mysql)
        {
            const bool createsRoutine = first == "CREATE" && (std::ranges::find(words, "PROCEDURE") != words.end() ||
                                                              std::ranges::find(words, "FUNCTION") != words.end() ||
                                                              std::ranges::find(words, "TRIGGER") != words.end() ||
                                                              std::ranges::find(words, "EVENT") != words.end());
            routine = routine || createsRoutine;
            constexpr std::string_view forbidden[]{"START",     "COMMIT", "ROLLBACK", "SAVEPOINT", "RELEASE",
                                                   "USE",       "RESET",  "LOCK",     "UNLOCK",    "FLUSH",
                                                   "DELIMITER", "SOURCE", "PREPARE",  "EXECUTE",   "DEALLOCATE",
                                                   "CONNECT",   "QUIT",   "EXIT",     "XA"};
            if (std::ranges::find(forbidden, first) != std::end(forbidden) ||
                ((first == "BEGIN" || first == "END") && !routine) ||
                (first == "SET" && (words.size() < 2 || words[1] != "NEW")))
            {
                invalid();
            }
            constexpr std::string_view forbiddenAnywhere[]{
                "DELIMITER",         "COMMIT",     "ROLLBACK", "SAVEPOINT", "START", "GET_LOCK", "RELEASE_LOCK",
                "RELEASE_ALL_LOCKS", "USE",        "RESET",    "FLUSH",     "LOCK",  "UNLOCK",   "PREPARE",
                "EXECUTE",           "DEALLOCATE", "XA"};
            for (const auto& word : forbiddenAnywhere)
            {
                if (std::ranges::find(words, word) != words.end())
                {
                    invalid();
                }
            }
            for (std::size_t index = 0; index < words.size(); ++index)
            {
                const auto& word = words[index];
                if (word == "DROP" && index + 1 < words.size() &&
                    (words[index + 1] == "DATABASE" || words[index + 1] == "SCHEMA"))
                {
                    invalid();
                }
                if (word == "SET" && !(index + 1 < words.size() && words[index + 1] == "NEW"))
                {
                    // SET belonging to UPDATE/INSERT changes rows; standalone SET
                    // changes the connection, including inside a stored routine.
                    bool mutation = false;
                    for (std::size_t before = 0; before < index; ++before)
                    {
                        if (words[before] == "BEGIN" || words[before] == "THEN" || words[before] == "ELSE")
                        {
                            mutation = false;
                        }
                        else if (words[before] == "UPDATE" || words[before] == "INSERT")
                        {
                            mutation = true;
                        }
                    }
                    if (!mutation)
                    {
                        invalid();
                    }
                }
                if (routine && (word == "BEGIN" || (word == "CASE" && (index == 0 || words[index - 1] != "END"))))
                {
                    blocks.push_back(word == "BEGIN" ? "BEGIN" : "CASE");
                }
                else if (routine && word == "END" &&
                         (index + 1 == words.size() || (words[index + 1] != "IF" && words[index + 1] != "LOOP" &&
                                                        words[index + 1] != "WHILE" && words[index + 1] != "REPEAT")))
                {
                    if (!blocks.empty())
                    {
                        blocks.pop_back();
                    }
                }
            }
            if (routine && blocks.empty())
            {
                routine = false;
            }
            return;
        }
        constexpr std::string_view forbidden[]{"BEGIN", "START",     "COMMIT",  "END",   "ROLLBACK",
                                               "ABORT", "SAVEPOINT", "RELEASE", "RESET", "DISCARD"};
        if (std::ranges::find(forbidden, first) != std::end(forbidden) ||
            (first == "SET" && (words.size() < 2 || words[1] != "CONSTRAINTS")) ||
            (first == "PREPARE" && words.size() > 1 && words[1] == "TRANSACTION"))
        {
            invalid();
        }
        for (std::size_t i = 1; i < words.size(); ++i)
        {
            if ((words[i - 1] == "BEGIN" && words[i] == "ATOMIC") ||
                (first == "COPY" &&
                 ((words[i - 1] == "FROM" && words[i] == "STDIN") || (words[i - 1] == "TO" && words[i] == "STDOUT"))))
            {
                invalid();
            }
        }
    };
    for (std::size_t i = 0; i < script.size();)
    {
        const auto c = script[i];
        if (c == '\\' || (mysql && c == '@'))
        {
            invalid();
        }
        if ((c == '-' && i + 1 < script.size() && script[i + 1] == '-' &&
             (!mysql || i + 2 == script.size() || static_cast<unsigned char>(script[i + 2]) <= 32)) ||
            (mysql && c == '#'))
        {
            const auto end = script.find_first_of("\r\n", i + (c == '#' ? 1 : 2));
            i = end == std::string_view::npos ? script.size() : end + 1;
        }
        else if (c == '/' && i + 1 < script.size() && script[i + 1] == '*')
        {
            std::size_t depth = 1;
            i += 2;
            while (i < script.size() && depth != 0)
            {
                if (!mysql && i + 1 < script.size() && script.substr(i, 2) == "/*")
                {
                    ++depth;
                    i += 2;
                }
                else if (i + 1 < script.size() && script.substr(i, 2) == "*/")
                {
                    --depth;
                    i += 2;
                }
                else
                {
                    ++i;
                }
            }
            if (depth != 0)
            {
                invalid();
            }
        }
        else if (c == '\'' || c == '"' || (backend == db::BackendType::Sqlite && (c == '`' || c == '[')) ||
                 (mysql && c == '`'))
        {
            const auto quotedStart = i;
            const auto quote = c == '[' ? ']' : c;
            const bool escapes = (mysql && c != '`') || (c == '\'' && backend == db::BackendType::Postgres && i > 0 &&
                                                         (script[i - 1] == 'E' || script[i - 1] == 'e') &&
                                                         (i < 2 || !identifier(script[i - 2])));
            ++i;
            bool closed = false;
            while (i < script.size())
            {
                if (escapes && script[i] == '\\')
                {
                    i += 2;
                }
                else if (script[i] == quote)
                {
                    ++i;
                    if (c != '[' && i < script.size() && script[i] == quote)
                    {
                        ++i;
                    }
                    else
                    {
                        closed = true;
                        break;
                    }
                }
                else
                {
                    ++i;
                }
            }
            if (!closed)
            {
                invalid();
            }
            const auto quoted = upper(script.substr(quotedStart + 1, i - quotedStart - 2));
            if (((c == '"' && backend == db::BackendType::Postgres) || (mysql && c == '`')) &&
                (quoted == "_ORM_MIGRATIONS" || quoted == "_ORM_MIGRATIONS_INCOMPLETE" ||
                 (mysql && (quoted == "GET_LOCK" || quoted == "RELEASE_LOCK" || quoted == "RELEASE_ALL_LOCKS"))))
            {
                invalid();
            }
            // Quoted identifiers must never turn SET "search_path" into SET CONSTRAINTS.
            words.emplace_back("<quoted>");
        }
        else if (c == '$' && backend == db::BackendType::Postgres)
        {
            auto end = i + 1;
            if (end < script.size() && letter(script[end]))
            {
                while (end < script.size() && (letter(script[end]) || (script[end] >= '0' && script[end] <= '9')))
                {
                    ++end;
                }
            }
            if (end < script.size() && script[end] == '$')
            {
                const auto tag = script.substr(i, end - i + 1);
                const auto close = script.find(tag, end + 1);
                if (close == std::string_view::npos)
                {
                    invalid();
                }
                i = close + tag.size();
                words.emplace_back("<body>");
            }
            else
            {
                ++i;
            }
        }
        else if (letter(c))
        {
            const auto start = i++;
            while (i < script.size() && identifier(script[i]))
            {
                ++i;
            }
            words.push_back(upper(script.substr(start, i - start)));
        }
        else if (c == ';')
        {
            check();
            words.clear();
            ++i;
        }
        else
        {
            ++i;
        }
    }
    check();
    // SQLite's native authorizer rejects transaction/session commands at preparation,
    // including commands after CREATE TRIGGER bodies, without guessing SQL grammar.
}
} // namespace orm::migrations::detail
