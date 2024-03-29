#pragma once

#include <string>

#include "orm-cxx/model/ColumnType.hpp"

namespace orm::db
{
class TypeTranslator
{
public:
    virtual ~TypeTranslator() = default;

    [[nodiscard]] virtual auto toSqlType(model::ColumnType type) const -> std::string = 0;
};
} // namespace orm::db
