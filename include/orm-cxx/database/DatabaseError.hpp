#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "BackendType.hpp"

namespace orm
{
enum class DatabaseErrorCode
{
    NotConnected,
    AlreadyConnected,
    UnsupportedBackend,
    UnsupportedFeature,
    Connection,
    Statement,
    Constraint,
    Transaction,
    Conversion,
    AffectedRowsUnavailable,
};

/**
 * @brief A backend-neutral database runtime error.
 *
 * The structured context intentionally excludes connection strings and bound
 * parameter values. Backend adapters are responsible for passing a sanitized
 * message to the constructor.
 */
class DatabaseError final : public std::runtime_error
{
public:
    DatabaseError(DatabaseErrorCode codeInit, db::BackendType backendTypeInit, std::string operationInit,
                  std::string message, std::optional<std::string> nativeCodeInit = std::nullopt)
        : std::runtime_error{std::move(message)},
          code{codeInit},
          backendType{backendTypeInit},
          operation{std::move(operationInit)},
          nativeCode{std::move(nativeCodeInit)}
    {
    }

    [[nodiscard]] auto getCode() const noexcept -> DatabaseErrorCode
    {
        return code;
    }

    [[nodiscard]] auto getBackendType() const noexcept -> db::BackendType
    {
        return backendType;
    }

    [[nodiscard]] auto getOperation() const noexcept -> const std::string&
    {
        return operation;
    }

    [[nodiscard]] auto getNativeCode() const noexcept -> const std::optional<std::string>&
    {
        return nativeCode;
    }

private:
    DatabaseErrorCode code;
    db::BackendType backendType;
    std::string operation;
    std::optional<std::string> nativeCode;
};
} // namespace orm
