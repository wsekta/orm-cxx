#pragma once

#include <string>

namespace orm::db
{
class TypeTranslator
{
public:
    virtual ~TypeTranslator() = default;

    virtual auto toSqlType(const std::string& type) const -> std::string = 0;

    virtual auto toCppType(const std::string& type) const -> std::string = 0;
};
}