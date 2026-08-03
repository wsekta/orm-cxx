#include "orm-cxx/model/ColumnType.hpp"

namespace orm::model
{
auto toString(ColumnType type) -> std::string
{
    switch (type)
    {
    case ColumnType::Bool:
        return "bool";
    case ColumnType::Char:
        return "char";
    case ColumnType::UnsignedChar:
        return "unsigned char";
    case ColumnType::Short:
        return "short";
    case ColumnType::UnsignedShort:
        return "unsigned short";
    case ColumnType::Int:
        return "int";
    case ColumnType::UnsignedInt:
        return "unsigned int";
    case ColumnType::LongLong:
        return "long long";
    case ColumnType::UnsignedLongLong:
        return "unsigned long long";
    case ColumnType::Float:
        return "float";
    case ColumnType::Double:
        return "double";
    case ColumnType::String:
        return "std::string";
    case ColumnType::Uuid:
        return "uuid";
    }

    return "unknown";
}
} // namespace orm::model
