#pragma once

#include "orm-cxx/database/TypeTranslator.hpp"

namespace orm::db::sqlite
{
class SqliteTypeTranslator : public TypeTranslator
{
public:
    auto toSqlType(const std::string& type) const -> std::string override;

    auto toCppType(const std::string& type) const -> std::string override;

private:
};
}