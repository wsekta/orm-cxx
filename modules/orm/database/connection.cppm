module;

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

export module orm:database_connection;

import :foundation;
import :connection_config;
import :model;
import :sql;
import :query_value;
import :database_payload;

namespace orm::migrations::detail
{
struct DatabaseAccess;
}

export namespace orm
{
class Database final
{
public:
    /**
     * @brief Constructs a new Database object.
     */
    template <db::ConnectionConfiguration Config>
    explicit Database(const Config& config, db::CommandGeneratorFactory registry = {})
        : Database{std::move(registry), db::ConnectionConfigView{config}}
    {
    }
    ~Database() noexcept;
    Database(const Database&) = delete;
    Database(Database&&) = delete;
    auto operator=(const Database&) -> Database& = delete;
    auto operator=(Database&&) -> Database& = delete;

    /**
     * @brief Connects to a database.
     *
     * @param config The typed connection parameters, borrowed during opening.
     */
    template <db::ConnectionConfiguration Config>
    auto connect(const Config& config) -> void
    {
        connect(db::ConnectionConfigView{config});
    }

    /**
     * @brief Disconnects from the database.
     */
    auto disconnect() -> void;

    /** Creates a borrowed ORM context. This Database must outlive the context. */
    template <typename SchemaType>
    [[nodiscard]] auto orm() & noexcept -> OrmContext<SchemaType>
    {
        return OrmContext<SchemaType>{*this};
    }

    template <typename SchemaType>
    auto orm() && -> OrmContext<SchemaType> = delete;

    /**
     * @brief Get the backend type of the database.
     *
     * @return The backend type of the database.
     */
    [[nodiscard]] auto getBackendType() const noexcept -> db::BackendType;

    /**
     * @brief Returns whether a backend session is currently open.
     */
    [[nodiscard]] auto isConnected() const noexcept -> bool;

    /**
     * @brief Returns the capabilities advertised by the connected backend.
     * @throws DatabaseError when no backend is connected.
     */
    [[nodiscard]] auto getBackendCapabilities() const -> const db::BackendCapabilities&;

    /**
     * @brief Starts a transaction.
     */
    auto beginTransaction() -> void;

    /**
     * @brief Commits a transaction.
     */
    auto commitTransaction() -> void;

    /**
     * @brief Rollbacks a transaction.
     */
    auto rollbackTransaction() -> void;

private:
    explicit Database(db::CommandGeneratorFactory registry, db::ConnectionConfigView config);
    auto connect(db::ConnectionConfigView config) -> void;
    friend struct migrations::detail::DatabaseAccess;
    template <typename>
    friend class OrmContext;

    auto executeMutation(const db::Statement& statement, std::string_view operation) -> std::size_t;
    auto executeMutation(db::StatementView statement, std::string_view operation) -> std::size_t;
    auto executeInsertIfAbsent(const db::Statement& statement, const db::InsertIfAbsentSpec& conflict,
                               std::string_view operation) -> std::size_t;
    auto executeSql(std::string_view statement, std::string_view operation) -> void;
    auto ensureSessionUsable(std::string_view operation) const -> void;
    auto ensureDdlSupported(std::string_view operation) const -> void;
    auto ensureSchemaSupported(model::ModelView model, bool includeJunctions, std::string_view operation) -> void;
    auto ensureStringValueSupported(const model::ColumnView& column, const std::optional<query::QueryValue>& value,
                                    std::string_view operation) const -> void;
    auto relationEndpointExists(model::ModelView model, const db::binding::PrimaryKey& key) -> bool;
    auto tableExists(std::string_view tableName) -> bool;
    auto ensureEndpointKeySupported(model::ModelView model, const db::binding::PrimaryKey& key,
                                    std::string_view operation) const -> void;
    auto ensureRelationTableEndpointsExist(model::ModelView owner) -> void;
    [[nodiscard]] auto getBackend() const -> const db::BackendProvider&;
    [[nodiscard]] auto getCommandGenerator() const -> const db::CommandGenerator&;
    [[nodiscard]] auto getBackendRuntimeLimits() -> db::BackendRuntimeLimits;
    auto ensureStatementWithinBindLimit(std::size_t parameterCount, std::string_view operation) -> void;
    auto ensureModelSupported(model::ModelView model, std::string_view operation) const -> void;
    auto ensureQuerySupported(model::ModelView model, const query::detail::SelectSpec& spec) const -> void;
    auto ensureQuerySupported(model::ModelView model, db::detail::SqlQueryView query) const -> void;
    auto ensurePredicateSupported(const query::detail::Predicate& predicate, std::string_view operation) const -> void;
    auto ensurePredicateSupported(bool containsCollection, std::string_view operation) const -> void;
    auto ensureAffectedRowsAvailable(std::string_view operation) const -> void;
    auto requireCapability(bool supported, std::string_view operation, std::string_view message) const -> void;
    [[noreturn]] auto throwTranslatedError(const soci::soci_error& error, DatabaseErrorCode fallback,
                                           std::string_view operation) -> void;

    soci::session sql;
    std::unique_ptr<soci::transaction> transaction;
    bool transactionFailed = false;
    bool migrationActive = false;
    db::BackendType backendType;
    db::CommandGeneratorFactory commandGeneratorFactory;
    const db::BackendProvider* backend = nullptr;
};
} // namespace orm
