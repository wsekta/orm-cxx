#pragma once

#include "orm-cxx/database/TypeTranslator.hpp"

namespace orm::db::postgresql
{
class PostgresqlTypeTranslator final : public TypeTranslator
{
public:
    [[nodiscard]] auto toSqlType(model::ColumnType type) const -> std::string override;
};
} // namespace orm::db::postgresql
