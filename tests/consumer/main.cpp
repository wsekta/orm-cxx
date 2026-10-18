#include <concepts>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import orm;
import orm_cxx_consumer.models;

auto consumerModelTypeId() -> orm::model::TypeId;
auto consumerModelView() -> orm::model::ModelView;

struct GlobalConsumerModel
{
    int id;
};

using GlobalConsumerSchema = orm::Schema<GlobalConsumerModel>;

static_assert(orm::reflection::fieldName<GlobalConsumerModel, 0>() == "id");
static_assert(orm::modelView<GlobalConsumerSchema, GlobalConsumerModel>()->tableName == "GlobalConsumerModel");
static_assert(orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>()->tableName ==
              "consumer_models_ConsumerModel");

namespace
{
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
        : commands{delegate.dialect()}, delegate{delegate}
    {
    }

    auto type() const noexcept -> orm::db::BackendType override
    {
        return orm::db::BackendType::Oracle;
    }
    auto acceptsConnectionString(std::string_view) const noexcept -> bool override
    {
        return false;
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
        return delegate.runtime();
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return commands;
    }

    ConsumerCommands commands;

private:
    const orm::db::BackendProvider& delegate;
};
} // namespace

int main()
try
{
    static_assert(orm::reflection::fieldCount<consumer_models::ConsumerModel> == 1);
    static_assert(orm::reflection::fieldName<consumer_models::ConsumerModel, 0>() == "id");

    if (consumerModelTypeId() != orm::model::typeId<consumer_models::ConsumerModel>() ||
        consumerModelView()->type != orm::model::typeId<consumer_models::ConsumerModel>() ||
        consumerModelView() != orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>())
    {
        return 4;
    }

    orm::Database database;
    const orm::migrations::Catalog migrations{};
    orm::migrations::Runner runner{database, migrations};
    (void)runner;
    const auto generatedSnapshot = orm::migrations::capture(consumer_models::Schema::view);
    const auto generation = orm::migrations::generate(
        1, "consumer_initial", orm::migrations::compare(orm::migrations::emptySnapshot(), generatedSnapshot));
    if (generation.migration.scripts.size() != 4)
    {
        return 5;
    }
    auto context = database.orm<consumer_models::Schema>();
    (void)context;
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
    const auto* sqlite = factory.findBackend("sqlite3://:memory:");
    const auto* postgresql = factory.findBackend("postgresql://host=localhost dbname=orm_cxx");
    const auto* mysql = factory.findBackend("mysql://host=localhost db=orm_cxx");
    const auto* mariadb = factory.findBackend("mariadb://host=localhost db=orm_cxx");

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
    const auto selected = generator.select(model, query.getData());
    if (not selected.sql.starts_with("/* consumer override */ SELECT ") or selected.parameters.size() != 1 or
        commands.selects != 1 or generator.createTable(model).empty() or generator.dropTable(model).empty() or
        generator.insert(model).empty() or generator.update(model, update.getData()).parameters.size() != 2 or
        generator.remove(model, *query.getData().predicate).parameters.size() != 1)
    {
        return 8;
    }

    if (sqlite != nullptr)
    {
        auto provider = std::make_unique<ConsumerBackend>(*sqlite);
        const auto* observed = provider.get();
        orm::db::CommandGeneratorFactory registry{std::vector<std::unique_ptr<orm::db::BackendProvider>>{}};
        registry.registerBackend(std::move(provider));
        orm::Database customDatabase{std::move(registry)};
        customDatabase.connect(orm::db::BackendType::Oracle, "sqlite3://:memory:");
        auto custom = customDatabase.orm<consumer_models::Schema>();
        custom.createTable<consumer_models::ConsumerModel>();
        custom.insert(consumer_models::ConsumerModel{7});
        if (observed->compiledSqlFlavor() != orm::db::CompiledSqlFlavor::None or
            custom.select(selectPlan, 7).size() != 1 or observed->commands.selects != 1 or
            custom.update(updatePlan, 8, 7) != 1 or custom.remove(removePlan, 8) != 1)
        {
            return 9;
        }
        custom.dropTable<consumer_models::ConsumerModel>();
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
