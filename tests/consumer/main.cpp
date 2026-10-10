#include <concepts>
#include <string>
#include <string_view>
#include <vector>

import orm;
import orm_cxx_consumer.models;

template <typename Owner>
consteval auto sqliteRelationOverloadsMatchConfiguration() -> bool
{
    constexpr bool create = requires(Owner owner) {
        { orm::db::relations::createTableStatements(owner) } -> std::same_as<std::vector<std::string>>;
    };
    constexpr bool drop = requires(Owner owner) {
        { orm::db::relations::dropTableStatements(owner) } -> std::same_as<std::vector<std::string>>;
    };
    constexpr bool link = requires(Owner owner, orm::model::RelationView relation, const orm::db::PrimaryKey& key) {
        { orm::db::relations::linkStatement(owner, relation, key, key) } -> std::same_as<orm::db::Statement>;
    };
    constexpr bool unlink = requires(Owner owner, orm::model::RelationView relation, const orm::db::PrimaryKey& key) {
        { orm::db::relations::unlinkStatement(owner, relation, key, key) } -> std::same_as<orm::db::Statement>;
    };
    constexpr bool select = requires(Owner owner, orm::model::RelationView relation, std::string sql,
                                     const std::vector<orm::db::PrimaryKey>& keys) {
        {
            orm::db::relations::collectionSelectStatement(owner, relation, sql, keys, true)
        } -> std::same_as<orm::db::Statement>;
    };
    constexpr bool enabled = orm::config::sqliteBackendEnabled;
    return create == enabled and drop == enabled and link == enabled and unlink == enabled and select == enabled;
}

static_assert(sqliteRelationOverloadsMatchConfiguration<orm::model::ModelView>());

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

    auto toSqlType(orm::model::ColumnType /*type*/) const -> std::string override
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
} // namespace

int main()
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

    if ((sqlite != nullptr) != orm::config::sqliteBackendEnabled)
    {
        return 1;
    }
    if ((postgresql != nullptr) != orm::config::postgresqlBackendEnabled or
        (postgresql != nullptr and postgresql->type() != orm::db::BackendType::Postgres))
    {
        return 2;
    }

    const ConsumerDialect dialect;

    if (not orm::db::relations::createTableStatements(
                dialect, orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>())
                .empty())
    {
        return 3;
    }

    return 0;
}
