#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

import orm;

namespace
{
class ImportedSelectCommand final : public orm::db::commands::SelectCommand
{
public:
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
};

class ImportedUpdateCommand final : public orm::db::commands::UpdateCommand
{
public:
    auto update(orm::model::ModelView, const orm::db::UpdateSpec& spec) const -> orm::db::Statement override
    {
        return {.sql = spec.assignments.empty() ? "UPDATE" : spec.assignments.front().column.getPath(),
                .parameters = {}};
    }
};

class ImportedDeleteCommand final : public orm::db::commands::DeleteCommand
{
public:
    auto remove(orm::model::ModelView, const orm::db::Predicate& predicate) const -> orm::db::Statement override
    {
        const auto& expression = predicate.getNode().expression;
        const auto* raw = std::get_if<orm::db::ast::RawExpression>(&expression);
        return {.sql = raw ? raw->sql : "DELETE", .parameters = {}};
    }
};

class ImportedProvider final : public orm::db::BackendProvider
{
public:
    explicit ImportedProvider(const orm::db::BackendProvider& delegate) : delegate{delegate} {}
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
        return delegate.commandGenerator();
    }

private:
    const orm::db::BackendProvider& delegate;
};

static_assert(not std::is_abstract_v<ImportedSelectCommand>);
static_assert(not std::is_abstract_v<ImportedUpdateCommand>);
static_assert(not std::is_abstract_v<ImportedDeleteCommand>);
static_assert(not std::is_abstract_v<ImportedProvider>);

[[maybe_unused]] auto importedBackendContract(orm::db::CommandGeneratorFactory& registry,
                                              const orm::db::BackendProvider& delegate) -> void
{
    registry.registerBackend(std::make_unique<ImportedProvider>(delegate));
    orm::db::PrimaryKey key{orm::query::QueryValue{1}};
    (void)key;
}
} // namespace
