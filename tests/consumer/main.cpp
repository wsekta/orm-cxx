#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace soci
{
class session;
class values;
class soci_error;
}

import orm;
import orm_cxx_consumer.models;

auto consumerModelTypeId() -> orm::model::TypeId;
auto consumerModelView() -> orm::model::ModelView;

struct GlobalConsumerModel
{
    int id;
};

using GlobalConsumerSchema = orm::Schema<GlobalConsumerModel>;

struct ConsumerNativeValues
{
    orm::Uuid id;
    orm::Date date;
    orm::TimeOfDay time;
    orm::LocalDateTime local;
    std::optional<orm::UtcTimestamp> instant;
};

struct ConsumerNativeProjection
{
    orm::Uuid id;
    std::optional<orm::UtcTimestamp> instant;
};

[[maybe_unused]] auto checkNativeValueApi(orm::Database& database, const ConsumerNativeValues& value) -> void
{
    using namespace orm::query;
    auto context = database.orm<orm::Schema<ConsumerNativeValues>>();
    context.insert(value);
    orm::Query<ConsumerNativeValues> query;
    query
        .where((col<&ConsumerNativeValues::date>() == value.date) && (col<&ConsumerNativeValues::time>() >= value.time))
        .orderBy(asc(col<&ConsumerNativeValues::local>()));
    (void)context.select(query);
    constexpr auto projected =
        selectAs<ConsumerNativeValues, ConsumerNativeProjection>(as<"instant">(col<&ConsumerNativeValues::instant>()),
                                                                 as<"id">(col<&ConsumerNativeValues::id>()))
            .where(col<&ConsumerNativeValues::id>() == param<orm::Uuid, 0>());
    (void)context.select(projected, value.id);
    constexpr auto change =
        update<ConsumerNativeValues>()
            .set(col<&ConsumerNativeValues::instant>(), param<std::optional<orm::UtcTimestamp>, 0>())
            .where(col<&ConsumerNativeValues::id>() == param<orm::Uuid, 1>());
    (void)context.update(change, value.instant, value.id);
}

static_assert(orm::reflection::fieldName<GlobalConsumerModel, 0>() == "id");
static_assert(orm::modelView<GlobalConsumerSchema, GlobalConsumerModel>()->tableName == "GlobalConsumerModel");
static_assert(orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>()->tableName ==
              "consumer_models_ConsumerModel");

struct ConsumerConfig
{
    orm::SqliteConfig sqlite;
};
template <>
struct orm::db::ConnectionTraits<ConsumerConfig>
{
    static constexpr auto backendType = orm::db::BackendType::Oracle;
};

[[maybe_unused]] auto checkBorrowedApi(orm::Database& database) -> void
{
    const orm::migrations::Catalog migrations{};
    orm::migrations::Runner runner{database, migrations};
    auto context = database.orm<consumer_models::Schema>();
    (void)runner;
    (void)context;
}

namespace
{
class ConsumerRuntime final : public orm::db::BackendRuntime
{
public:
    explicit ConsumerRuntime(const orm::db::BackendRuntime& delegate) : delegate{delegate} {}
    auto open(soci::session& session, orm::db::ConnectionConfigView view) const -> void override
    {
        const auto& config = view.get<ConsumerConfig>();
        delegate.open(session, orm::db::ConnectionConfigView{config.sqlite});
    }
    auto onConnect(soci::session& s) const -> void override
    {
        delegate.onConnect(s);
    }
    auto tableExists(soci::session& s, std::string_view n) const -> bool override
    {
        return delegate.tableExists(s, n);
    }
    auto limits(soci::session& s) const -> orm::db::BackendRuntimeLimits override
    {
        return delegate.limits(s);
    }
    auto normalizeAffectedRows(long long n) const -> std::size_t override
    {
        return delegate.normalizeAffectedRows(n);
    }
    auto bind(soci::values& v, std::string_view n, const orm::db::BoundValue& b) const -> void override
    {
        delegate.bind(v, n, b);
    }
    auto translateError(const soci::soci_error& e, orm::DatabaseErrorCode c,
                        std::string_view op) const -> orm::DatabaseError override
    {
        return delegate.translateError(e, c, op);
    }

private:
    const orm::db::BackendRuntime& delegate;
};

class ConsumerDialect final : public orm::db::SqlDialect
{
public:
    auto quoteIdentifier(std::string_view identifier) const -> std::string override
    {
        return std::string{identifier};
    }

    auto bindMarker(std::string_view logicalName) const -> std::string override
    {
        return ":" + std::string{logicalName};
    }

    auto toSqlType(orm::model::ColumnType /*type*/,
                   std::optional<std::size_t> /*maxStringLength*/) const -> std::string override
    {
        return "INTEGER";
    }

    auto renderCreateTablePrefix(std::string_view tableName, bool /*ifNotExists*/) const -> std::string override
    {
        return "CREATE TABLE " + std::string{tableName} + " (";
    }

    auto renderDropTable(std::string_view tableName, bool /*ifExists*/) const -> std::string override
    {
        return "DROP TABLE " + std::string{tableName} + ";";
    }

    auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override
    {
        return std::string{columnName} + " INTEGER PRIMARY KEY";
    }

    auto renderPagination(const orm::db::PaginationSpec& /*pagination*/) const -> std::string override
    {
        return {};
    }

    auto renderInsertIfAbsent(const orm::db::InsertIfAbsentSpec& /*insert*/) const -> std::string override
    {
        return {};
    }
};

class ConsumerCommands final : public orm::db::DefaultCommandGenerator
{
public:
    using DefaultCommandGenerator::DefaultCommandGenerator;

    auto select(orm::model::ModelView model, const orm::db::SelectSpec& spec) const -> orm::db::SelectStatement override
    {
        ++selects;
        auto statement = DefaultCommandGenerator::select(model, spec);
        statement.sql.insert(0, "/* consumer override */ ");
        return statement;
    }

    mutable int selects{};
};

class ConsumerBackend final : public orm::db::BackendProvider
{
public:
    explicit ConsumerBackend(const orm::db::BackendProvider& delegate)
        : commands{delegate.dialect()}, delegate{delegate}, runtime_{delegate.runtime()}
    {
    }

    auto type() const noexcept -> orm::db::BackendType override
    {
        return orm::db::BackendType::Oracle;
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
        return runtime_;
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return commands;
    }

    ConsumerCommands commands;

private:
    const orm::db::BackendProvider& delegate;
    ConsumerRuntime runtime_;
};
} // namespace

int main()
try
{
    const orm::Date date{std::chrono::year{2026}, std::chrono::month{10}, std::chrono::day{24}};
    const orm::LocalDateTime local{std::chrono::microseconds{123456}};
    const orm::UtcTimestamp utc{std::chrono::microseconds{123456}};
    const orm::TimeOfDay time{std::chrono::microseconds{123456}};
    const auto uuid = orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff");
    if (!date.ok() || local.time_since_epoch() != time.sinceMidnight() ||
        utc.time_since_epoch() != time.sinceMidnight() || orm::Uuid{uuid.bytes()} != uuid ||
        uuid.toString() != "00112233-4455-6677-8899-aabbccddeeff")
    {
        throw std::runtime_error{"Public value type round trip failed"};
    }

    static_assert(orm::reflection::fieldCount<consumer_models::ConsumerModel> == 1);
    static_assert(orm::reflection::fieldName<consumer_models::ConsumerModel, 0>() == "id");

    if (consumerModelTypeId() != orm::model::typeId<consumer_models::ConsumerModel>() ||
        consumerModelView()->type != orm::model::typeId<consumer_models::ConsumerModel>() ||
        consumerModelView() != orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>())
    {
        return 4;
    }

    const auto generatedSnapshot = orm::migrations::capture(consumer_models::Schema::view);
    const auto generation = orm::migrations::generate(
        1, "consumer_initial", orm::migrations::compare(orm::migrations::emptySnapshot(), generatedSnapshot));
    if (generation.migration.scripts.size() != 4)
    {
        return 5;
    }
    orm::Query<consumer_models::ConsumerModel> query;
    query.where(orm::query::col<&consumer_models::ConsumerModel::id>() == 7)
        .orderBy(orm::query::asc(orm::query::col<&consumer_models::ConsumerModel::id>()));
    orm::Update<consumer_models::ConsumerModel> update;
    update.set(orm::query::col<&consumer_models::ConsumerModel::id>(), short{8})
        .where(orm::query::col<&consumer_models::ConsumerModel::id>() == 7);
    orm::ProjectionQuery<consumer_models::ConsumerModel, consumer_models::ConsumerSummary> projection;
    projection.project(orm::query::as("id", orm::query::col<&consumer_models::ConsumerModel::id>()))
        .where(orm::query::col<&consumer_models::ConsumerModel::id>() >= short{1});

    using namespace orm::query;
    constexpr auto selectPlan =
        select<consumer_models::ConsumerModel>().where(col<&consumer_models::ConsumerModel::id>() == param<int, 0>());
    constexpr auto projectionPlan = selectAs<consumer_models::ConsumerModel, consumer_models::ConsumerSummary>(
        as<"id">(col<&consumer_models::ConsumerModel::id>()));
    constexpr auto updatePlan = orm::query::update<consumer_models::ConsumerModel>()
                                    .set(col<&consumer_models::ConsumerModel::id>(), param<int, 0>())
                                    .where(col<&consumer_models::ConsumerModel::id>() == param<int, 1>());
    constexpr auto removePlan =
        remove<consumer_models::ConsumerModel>().where(col<&consumer_models::ConsumerModel::id>() == param<int, 0>());
    (void)selectPlan.toDynamic(7);
    (void)projectionPlan.toDynamic();
    (void)updatePlan.toDynamic(8, 7);
    (void)removePlan.toDynamic(7);

    const orm::db::CommandGeneratorFactory factory;
    const auto registered = [&factory](orm::db::BackendType type) -> const orm::db::BackendProvider*
    {
        try
        {
            return &factory.getBackend(type);
        }
        catch (const std::out_of_range&)
        {
            return nullptr;
        }
    };
    const auto* sqlite = registered(orm::db::BackendType::Sqlite);
    const auto* postgresql = registered(orm::db::BackendType::Postgres);
    const auto* mysql = registered(orm::db::BackendType::Mysql);
    const auto* mariadb = registered(orm::db::BackendType::Mariadb);

    if ((sqlite != nullptr) != orm::config::sqliteBackendEnabled)
    {
        return 1;
    }
    if ((postgresql != nullptr) != orm::config::postgresqlBackendEnabled or
        (postgresql != nullptr and postgresql->type() != orm::db::BackendType::Postgres))
    {
        return 2;
    }

    if ((mysql != nullptr) != orm::config::mysqlBackendEnabled or
        (mysql != nullptr and mysql->type() != orm::db::BackendType::Mysql))
    {
        return 5;
    }
    if ((mariadb != nullptr) != orm::config::mariadbBackendEnabled or
        (mariadb != nullptr and mariadb->type() != orm::db::BackendType::Mariadb))
    {
        return 6;
    }
    const ConsumerDialect dialect;
    const auto model = orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>();
    const ConsumerCommands commands{dialect};
    const orm::db::CommandGenerator& generator = commands;
    const auto predicate = orm::db::ast::Column{"id"} == 7;
    orm::db::SelectSpec selectSpec;
    selectSpec.predicate = predicate;
    orm::db::UpdateSpec updateSpec;
    updateSpec.assignments.push_back(
        {.column = orm::db::ast::Column{"id"}, .value = {.value = orm::query::QueryValue{8}}});
    updateSpec.predicate = predicate;
    const auto selected = generator.select(model, selectSpec);
    if (not selected.sql.starts_with("/* consumer override */ SELECT ") or selected.parameters.size() != 1 or
        commands.selects != 1 or generator.createTable(model).empty() or generator.dropTable(model).empty() or
        generator.insert(model).empty() or generator.update(model, updateSpec).parameters.size() != 2 or
        generator.remove(model, predicate).parameters.size() != 1)
    {
        return 8;
    }

    if (sqlite != nullptr)
    {
        auto provider = std::make_unique<ConsumerBackend>(*sqlite);
        const auto* observed = provider.get();
        orm::db::CommandGeneratorFactory registry{std::vector<std::unique_ptr<orm::db::BackendProvider>>{}};
        registry.registerBackend(std::move(provider));
        orm::Database customDatabase{ConsumerConfig{orm::SqliteConfig{.database = orm::SqliteMemory{}}},
                                     std::move(registry)};
        auto custom = customDatabase.orm<consumer_models::Schema>();
        custom.createTable<consumer_models::ConsumerModel>();
        custom.insert(consumer_models::ConsumerModel{7});
        if (observed->compiledSqlFlavor() != orm::db::CompiledSqlFlavor::None or
            custom.select(selectPlan, 7).size() != 1 or observed->commands.selects != 1 or
            custom.update(updatePlan, 8, 7) != 1 or custom.remove(removePlan, 8) != 1)
        {
            return 9;
        }
        custom.deleteTable<consumer_models::ConsumerModel>();
    }

    if (not orm::db::relations::createTableStatements(
                dialect, orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>())
                .empty())
    {
        return 3;
    }

    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "Consumer validation failed: " << error.what() << '\n';
    return 7;
}
