#include "orm-cxx/model/ColumnType.hpp"

#include <regex>

namespace
{
const std::regex optionalRegex(R"((class )?std\:\:optional\<(.*)\>)");
const std::regex stringRegex{R"((class )?std.*::basic_string<.*>\W?)"};
}

namespace orm::model
{
auto toColumnType(const std::string& type) -> std::pair<ColumnType, bool>
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
    else if (typeString == "unsigned char")
    {
        columnType = ColumnType::UnsignedChar;
    }
    else if (typeString == "short")
    {
        columnType = ColumnType::Short;
    }
    else if (typeString == "unsigned short")
    {
        columnType = ColumnType::UnsignedShort;
    }
    else if (typeString == "long long" or typeString == "__int64")
    {
        columnType = ColumnType::LongLong;
    }
    else if (typeString == "unsigned long long" or typeString == "unsigned __int64")
    {
        columnType = ColumnType::UnsignedLongLong;
    }
    else if (typeString == "bool")
    {
       columnType = ColumnType::Bool;
    }
    else if (typeString == "int" or typeString == "long")
    {
        columnType = ColumnType::Int;
    }
    else if (typeString == "unsigned int" or typeString == "unsigned long")
    {
        columnType = ColumnType::UnsignedInt;
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
    // TODO: handle uuid, date, etc. types
    else
    {
        columnType = ColumnType::Unknown;
    }

    return {columnType, isNotNull};
}
} // namespace orm::model
