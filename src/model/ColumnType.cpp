#include "orm-cxx/model/ColumnType.hpp"

#include <regex>

namespace
{
const std::regex optionalRegex("(class )?std\\:\\:optional\\<(.*)\\>");
const std::regex stringRegex{R"((class )?std.*::basic_string<.*>\W?)"};
}

namespace orm::model
{
std::pair<ColumnType, bool> toColumnType(const std::string& type)
{
    bool isNotNull{};
    std::string typeString{};
    ColumnType columnType{};
    if (std::regex_match(type, optionalRegex))
    {
        typeString = std::regex_replace(type, optionalRegex, "$2");
        isNotNull = false;
    }
    else
    {
        typeString = type;
        isNotNull = true;
    }

    if (typeString == "char")
    {
        columnType = ColumnType::Char;
    }
    else if (typeString == "int")
    {
        columnType = ColumnType::Int;
    }
    else if (typeString == "float")
    {
        columnType = ColumnType::Float;
    }
    else if (typeString == "double")
    {
        columnType = ColumnType::Double;
    }
    else if (std::regex_match(type, stringRegex))
    {
        columnType = ColumnType::String;
    }
    //TODO: handle uuid, date, etc. types
    else
    {
        //TODO: handle model type, then throw error if not handled properly
        throw std::runtime_error("Unknown type: " + typeString);
    }

    return {columnType, isNotNull};
}
}