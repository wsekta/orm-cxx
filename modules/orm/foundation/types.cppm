module;

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

export module orm:foundation_types;

// model/ColumnType.hpp
namespace orm::model
{
export
{

    enum class ColumnType
    {
        Bool,
        Char,
        UnsignedChar,
        Short,
        UnsignedShort,
        Int,
        UnsignedInt,
        LongLong,
        UnsignedLongLong,
        Float,
        Double,
        String,
        Uuid,
    };

    auto toString(ColumnType type) -> std::string;
}
} // namespace orm::model

// model/RelationKind.hpp
namespace orm::model
{
export
{

    enum class RelationKind
    {
        ToOne,
        OneToMany,
        ManyToMany,
    };
}
} // namespace orm::model

// database/BackendType.hpp
namespace orm::db
{
export
{

    enum class BackendType
    {
        Sqlite,
        Postgres [[maybe_unused]],
        Mysql [[maybe_unused]],
        Oracle [[maybe_unused]],
        Firebird [[maybe_unused]],
        Db2 [[maybe_unused]],
        Odbc [[maybe_unused]],
        Empty
    };
}
} // namespace orm::db

// database/CompiledSqlFlavor.hpp
namespace orm::db
{
export
{

    enum class CompiledSqlFlavor
    {
        None,
        SQLite,
        PostgreSQL,
    };
}
} // namespace orm::db

// database/DatabaseError.hpp
namespace orm
{
export
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
}
} // namespace orm
