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
} // namespace

auto validateScript(std::string_view script, db::BackendType backend, Version version) -> void
{
    const auto invalid = [=]
    {
        throw MigrationError{ErrorCode::InvalidSql, "SQL contains unsupported transaction, session or client commands",
                             version, backend};
    };
    std::vector<std::string> words;
    const auto check = [&]
    {
        if (backend != db::BackendType::Postgres || words.empty())
        {
            return;
        }
        const auto& first = words.front();
        if (std::ranges::find(words, "_ORM_MIGRATIONS") != words.end())
        {
            invalid();
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
        if (c == '\\')
        {
            invalid();
        }
        if (c == '-' && i + 1 < script.size() && script[i + 1] == '-')
        {
            const auto end = script.find('\n', i + 2);
            i = end == std::string_view::npos ? script.size() : end + 1;
        }
        else if (c == '/' && i + 1 < script.size() && script[i + 1] == '*')
        {
            std::size_t depth = 1;
            i += 2;
            while (i < script.size() && depth != 0)
            {
                if (i + 1 < script.size() && script.substr(i, 2) == "/*")
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
        else if (c == '\'' || c == '"' || (backend == db::BackendType::Sqlite && (c == '`' || c == '[')))
        {
            const auto quotedStart = i;
            const auto quote = c == '[' ? ']' : c;
            const bool escapes = c == '\'' && backend == db::BackendType::Postgres && i > 0 &&
                                 (script[i - 1] == 'E' || script[i - 1] == 'e') &&
                                 (i < 2 || !identifier(script[i - 2]));
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
            if (c == '"' && backend == db::BackendType::Postgres &&
                upper(script.substr(quotedStart + 1, i - quotedStart - 2)) == "_ORM_MIGRATIONS")
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
