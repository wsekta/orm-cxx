#pragma once

#include "orm-cxx/database/TypeTranslator.hpp"

namespace orm::db::sqlite
{
class SqliteTypeTranslator : public TypeTranslator
{
public:
    [[nodiscard]] auto toSqlType(model::ColumnType type) const -> std::string override;
};
}
