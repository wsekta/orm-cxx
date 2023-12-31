#include "SqliteTypeTranslator.hpp"

#include <regex>
#include <stdexcept>

namespace
{
const std::regex stringRegex{R"((class )?std.*::basic_string<.*>\W?)"};
}

namespace orm::db::sqlite
{
auto SqliteTypeTranslator::toSqlType(const std::string& type) const -> std::string
{
    if (type == "int")
    {
        return "INTEGER";
    }
    else if (type == "double")
    {
        return "REAL";
    }
    else if (std::regex_match(type, stringRegex))
    {
        return "TEXT";
    }
    else
    {
        throw std::runtime_error("Unknown type: " + type);
    }
}

auto SqliteTypeTranslator::toCppType(const std::string& type) const -> std::string
{
    if (type == "INTEGER")
    {
        return "int";
    }
    else if (type == "TEXT")
    {
        return "string";
    }
    else if (type == "REAL")
    {
        return "double";
    }
    else if (type == "BLOB")
    {
        return "blob";
    }
    else
    {
        throw std::runtime_error("Unknown type: " + type);
    }
}
}