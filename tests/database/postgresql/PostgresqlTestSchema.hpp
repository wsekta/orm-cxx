#pragma once

namespace postgresql_test
{
inline constexpr std::string_view schemaPrefix{"orm_cxx_test_"};

inline auto isSafeSchemaName(std::string_view schemaName) -> bool
{
    if (not schemaName.starts_with(schemaPrefix) or schemaName.size() <= schemaPrefix.size() or
        schemaName.size() > 63 or schemaName == "public")
    {
        return false;
    }

    for (const auto character : schemaName)
    {
        const auto lowerCase = character >= 'a' and character <= 'z';
        const auto digit = character >= '0' and character <= '9';
        if (not lowerCase and not digit and character != '_')
        {
            return false;
        }
    }

    return true;
}

inline auto generateSchemaName() -> std::string
{
    static std::atomic<unsigned long long> counter{0};
    const auto timestamp =
        static_cast<unsigned long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto entropy = (static_cast<unsigned long long>(std::random_device{}()) << 32U) ^
                         static_cast<unsigned long long>(std::random_device{}());

    std::ostringstream stream;
    stream << schemaPrefix << std::hex << std::setw(16) << std::setfill('0')
           << (timestamp ^ entropy ^ counter.fetch_add(1, std::memory_order_relaxed));
    return stream.str();
}

inline auto quotedSchemaName(std::string_view schemaName) -> std::string
{
    if (not isSafeSchemaName(schemaName))
    {
        throw std::invalid_argument{"Refusing to use an unsafe PostgreSQL test schema name"};
    }

    return "\"" + std::string{schemaName} + "\"";
}

class SchemaRuntime : public orm::db::BackendRuntime
{
public:
    SchemaRuntime(const orm::db::BackendRuntime& backend, std::string schema)
        : delegate{backend}, schemaName{std::move(schema)}
    {
    }
    auto open(soci::session& session, orm::db::ConnectionConfigView config) const -> void override
    {
        delegate.open(session, config);
    }
    auto onConnect(soci::session& session) const -> void override
    {
        delegate.onConnect(session);
        session << "SET search_path TO " + quotedSchemaName(schemaName);
    }
    auto tableExists(soci::session& session, std::string_view name) const -> bool override
    {
        return delegate.tableExists(session, name);
    }
    auto limits(soci::session& session) const -> orm::db::BackendRuntimeLimits override
    {
        return delegate.limits(session);
    }
    auto normalizeAffectedRows(long long rows) const -> std::size_t override
    {
        return delegate.normalizeAffectedRows(rows);
    }
    auto bind(soci::values& values, std::string_view name, const orm::db::BoundValue& value) const -> void override
    {
        delegate.bind(values, name, value);
    }
    auto translateError(const soci::soci_error& error, orm::DatabaseErrorCode fallback,
                        std::string_view operation) const -> orm::DatabaseError override
    {
        return delegate.translateError(error, fallback, operation);
    }
    auto statementErrorInvalidatesTransaction(const soci::soci_error& error) const noexcept -> bool override
    {
        return delegate.statementErrorInvalidatesTransaction(error);
    }
    auto executeInsertIfAbsent(soci::session& session, orm::db::StatementView statement,
                               const orm::db::InsertIfAbsentSpec& spec) const -> std::size_t override
    {
        return delegate.executeInsertIfAbsent(session, statement, spec);
    }
    auto validateMigrationSession(soci::session& session) const -> void override
    {
        delegate.validateMigrationSession(session);
    }
    auto migrationTable(soci::session& session) const -> std::string override
    {
        return delegate.migrationTable(session);
    }
    auto beginMigration(soci::session& session, std::string_view table) const -> void override
    {
        delegate.beginMigration(session, table);
    }
    auto executeMigrationScript(soci::session& session, std::string_view sql) const -> void override
    {
        delegate.executeMigrationScript(session, sql);
    }
    auto migrationHistoryExists(soci::session& session, std::string_view table) const -> bool override
    {
        return delegate.migrationHistoryExists(session, table);
    }
    auto createMigrationHistory(soci::session& session, std::string_view table) const -> void override
    {
        delegate.createMigrationHistory(session, table);
    }
    auto migrationJournalTable(soci::session& session, std::string_view table) const -> std::string override
    {
        return delegate.migrationJournalTable(session, table);
    }
    auto endMigration(soci::session& session, std::string_view table) const -> void override
    {
        delegate.endMigration(session, table);
    }

private:
    const orm::db::BackendRuntime& delegate;
    std::string schemaName;
};

class SchemaBackend final : public orm::db::BackendProvider
{
public:
    explicit SchemaBackend(std::string schema)
        : delegate{backends.getBackend(orm::db::BackendType::Postgres)},
          schemaRuntime{delegate.runtime(), std::move(schema)}
    {
    }
    auto type() const noexcept -> orm::db::BackendType override
    {
        return delegate.type();
    }
    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return delegate.capabilities();
    }
    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return delegate.dialect();
    }
    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return schemaRuntime;
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return delegate.commandGenerator();
    }
    auto compiledSqlFlavor() const noexcept -> orm::db::CompiledSqlFlavor override
    {
        return delegate.compiledSqlFlavor();
    }

private:
    orm::db::CommandGeneratorFactory backends;
    const orm::db::BackendProvider& delegate;
    SchemaRuntime schemaRuntime;
};

class PostgresqlTestSchema
{
public:
    explicit PostgresqlTestSchema(orm::PostgresConfig config)
        : configValue{std::move(config)}, schemaNameValue{generateSchemaName()}
    {
        const orm::db::CommandGeneratorFactory backends;
        const auto& runtime = backends.getBackend(orm::db::BackendType::Postgres).runtime();
        runtime.open(adminSession, orm::db::ConnectionConfigView{configValue});
        runtime.onConnect(adminSession);
        adminSession << "CREATE SCHEMA " + quotedSchemaName(schemaNameValue);
        active = true;
    }
    PostgresqlTestSchema(const PostgresqlTestSchema&) = delete;
    PostgresqlTestSchema(PostgresqlTestSchema&&) = delete;
    auto operator=(const PostgresqlTestSchema&) -> PostgresqlTestSchema& = delete;
    auto operator=(PostgresqlTestSchema&&) -> PostgresqlTestSchema& = delete;
    ~PostgresqlTestSchema()
    {
        try
        {
            drop();
        }
        catch (...)
        {
        }
    }
    [[nodiscard]] auto connectionConfig() const -> orm::PostgresConfig
    {
        return configValue;
    }
    [[nodiscard]] auto baseConfig() const noexcept -> const orm::PostgresConfig&
    {
        return configValue;
    }
    [[nodiscard]] auto registry() const -> orm::db::CommandGeneratorFactory
    {
        std::vector<std::unique_ptr<orm::db::BackendProvider>> providers;
        providers.push_back(std::make_unique<SchemaBackend>(schemaNameValue));
        return orm::db::CommandGeneratorFactory{std::move(providers)};
    }
    [[nodiscard]] auto schemaName() const noexcept -> const std::string&
    {
        return schemaNameValue;
    }
    auto prepare(soci::session& session) const -> void
    {
        session << "SET search_path TO " + quotedSchemaName(schemaNameValue);
    }
    auto admin() noexcept -> soci::session&
    {
        return adminSession;
    }
    auto drop() -> void
    {
        if (!active)
            return;
        adminSession << "DROP SCHEMA " + quotedSchemaName(schemaNameValue) + " CASCADE";
        active = false;
        adminSession.close();
    }

private:
    orm::PostgresConfig configValue;
    std::string schemaNameValue;
    soci::session adminSession;
    bool active = false;
};
} // namespace postgresql_test
