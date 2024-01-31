#include "orm-cxx/model/ColumnType.hpp"

#include <iostream>
#include <regex>
#include <unordered_map>

namespace
{
const std::regex optionalRegex(R"((class )?std\:\:optional\<(.*)\>)");
const std::regex stringRegex{R"((class )?std.*::basic_string<.*>\W?)"};

const std::unordered_map<orm::model::ColumnType, std::string> typeToStringMaping{
    {orm::model::ColumnType::Bool, "bool"},
    {orm::model::ColumnType::Char, "char"},
    {orm::model::ColumnType::UnsignedChar, "unsigned char"},
    {orm::model::ColumnType::Short, "short"},
    {orm::model::ColumnType::UnsignedShort, "unsigned short"},
    {orm::model::ColumnType::Int, "int"},
    {orm::model::ColumnType::UnsignedInt, "unsigned int"},
    {orm::model::ColumnType::LongLong, "long long"},
    {orm::model::ColumnType::UnsignedLongLong, "unsigned long long"},
    {orm::model::ColumnType::Float, "float"},
    {orm::model::ColumnType::Double, "double"},
    {orm::model::ColumnType::String, "std::string"},
    {orm::model::ColumnType::Uuid, "uuid"},
    {orm::model::ColumnType::Unknown, "unknown"},
    {orm::model::ColumnType::OneToOne, "one to one"},
};
} // namespace

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

    if (typeString == "char" or typeString == "signed char")
    {
        columnType = ColumnType::Char;
    }
    else if (typeString == "unsigned char")
    {
        columnType = ColumnType::UnsignedChar;
    }
    else if (typeString == "short" or typeString == "short int")
    {
        columnType = ColumnType::Short;
    }
    else if (typeString == "unsigned short" or typeString == "short unsigned int")
    {
        columnType = ColumnType::UnsignedShort;
    }
    else if (typeString == "long long" or typeString == "long long int" or typeString == "__int64")
    {
        columnType = ColumnType::LongLong;
    }
    else if (typeString == "unsigned long long" or typeString == "long long unsigned int" or
             typeString == "unsigned __int64")
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
        std::cerr << "Wnknown type: " << type << std::endl;
        columnType = ColumnType::Unknown;
    }

    return {columnType, isNotNull};
}

auto toString(ColumnType type) -> std::string
{
    return typeToStringMaping.at(type);
}
} // namespace orm::model
