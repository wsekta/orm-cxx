#pragma once

#include <cstddef>
#include <string_view>

#include "BackendCapabilities.hpp"
#include "DatabaseError.hpp"
#include "Statement.hpp"

namespace soci
{
class session;
class soci_error;
class values;
}

namespace orm::db
{
class BackendRuntime
{
public:
    virtual ~BackendRuntime() = default;

    virtual auto onConnect(soci::session& session) const -> void = 0;
    [[nodiscard]] virtual auto tableExists(soci::session& session, std::string_view tableName) const -> bool = 0;
    [[nodiscard]] virtual auto limits(soci::session& session) const -> BackendRuntimeLimits = 0;
    [[nodiscard]] virtual auto normalizeAffectedRows(long long affectedRows) const -> std::size_t = 0;
    virtual auto bind(soci::values& values, std::string_view name, const BoundValue& value) const -> void = 0;
    [[nodiscard]] virtual auto translateError(const soci::soci_error& error, DatabaseErrorCode fallback,
                                              std::string_view operation) const -> DatabaseError = 0;
};
} // namespace orm::db
