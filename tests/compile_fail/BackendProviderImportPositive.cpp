#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

import orm;

namespace
{
class ImportedGenerator final : public orm::db::CommandGenerator
{
public:
    auto createTable(orm::model::ModelView) const -> std::string override
    {
        return "CREATE TABLE imported(id INT)";
    }
    auto dropTable(orm::model::ModelView) const -> std::string override
    {
        return "DROP TABLE imported";
    }
    auto insert(orm::model::ModelView) const -> std::string override
    {
        return "INSERT INTO imported VALUES(1)";
    }
    auto select(orm::model::ModelView, const orm::db::SelectSpec& spec) const -> orm::db::SelectStatement override
    {
        for (const auto& order : spec.orderBy)
        {
            if (order.direction == orm::db::ast::OrderDirection::Desc)
                return {.sql = order.column.getPath(), .parameters = {}};
        }
        if (spec.predicate.has_value())
        {
            const auto& expression = spec.predicate->getNode().expression;
            if (const auto* comparison = std::get_if<orm::db::ast::ComparisonExpression>(&expression))
            {
                if (comparison->comparisonOperator == orm::db::ast::ComparisonOperator::Equal)
                    return {.sql = comparison->column.getPath(), .parameters = {}};
            }
        }
        return {.sql = "SELECT 1", .parameters = {}};
    }
    auto update(orm::model::ModelView, const orm::db::UpdateSpec& spec) const -> orm::db::Statement override
    {
        return {.sql = spec.assignments.empty() ? "UPDATE" : spec.assignments.front().column.getPath(),
                .parameters = {}};
    }
    auto remove(orm::model::ModelView, const orm::db::Predicate& predicate) const -> orm::db::Statement override
    {
        const auto& expression = predicate.getNode().expression;
        const auto* raw = std::get_if<orm::db::ast::RawExpression>(&expression);
        return {.sql = raw ? raw->sql : "DELETE", .parameters = {}};
    }
};

class ImportedPartialGenerator final : public orm::db::DefaultCommandGenerator
{
public:
    using DefaultCommandGenerator::DefaultCommandGenerator;
    auto select(orm::model::ModelView model, const orm::db::SelectSpec& spec) const -> orm::db::SelectStatement override
    {
        return DefaultCommandGenerator::select(model, spec);
    }
};

class ImportedProvider final : public orm::db::BackendProvider
{
public:
    ImportedProvider(const orm::db::BackendProvider& delegate, std::unique_ptr<orm::db::CommandGenerator> generator)
        : delegate{delegate}, generator{std::move(generator)}
    {
    }
    auto type() const noexcept -> orm::db::BackendType override
    {
        return delegate.type();
    }
    auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool override
    {
        return delegate.acceptsConnectionString(connectionString);
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
        return *generator;
    }

private:
    const orm::db::BackendProvider& delegate;
    std::unique_ptr<orm::db::CommandGenerator> generator;
};

static_assert(std::is_abstract_v<orm::db::CommandGenerator>);
static_assert(std::has_virtual_destructor_v<orm::db::CommandGenerator>);
static_assert(not std::is_abstract_v<ImportedGenerator>);
static_assert(not std::is_abstract_v<ImportedPartialGenerator>);
static_assert(not std::is_abstract_v<ImportedProvider>);

[[maybe_unused]] auto importedBackendContract(orm::db::CommandGeneratorFactory& registry,
                                              const orm::db::BackendProvider& delegate) -> void
{
    registry.registerBackend(std::make_unique<ImportedProvider>(delegate, std::make_unique<ImportedGenerator>()));
    orm::db::PrimaryKey key{orm::query::QueryValue{1}};
    (void)key;
}

[[maybe_unused]] auto importedPartialBackendContract(orm::db::CommandGeneratorFactory& registry,
                                                     const orm::db::BackendProvider& delegate) -> void
{
    registry.registerBackend(
        std::make_unique<ImportedProvider>(delegate, std::make_unique<ImportedPartialGenerator>(delegate.dialect())));
}
} // namespace
