module;

#include <cstddef>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module orm:sql_commands;

import :foundation;
import :model;
import :sql_contracts;

export namespace orm::db
{
using SelectStatement = Statement;

struct InsertManySpec
{
    std::size_t rowCount{};

    auto operator==(const InsertManySpec&) const -> bool = default;
};

/** Stable parameter name for one row of a batch insert; the row index scopes it inside a chunk. */
[[nodiscard]] inline auto insertManyParameterName(std::size_t rowIndex, std::string_view logicalName) -> std::string
{
    return std::format("orm_m{}_{}", rowIndex, logicalName);
}

namespace detail
{
/** One bound insert column: its logical parameter name and the physical column it maps to. */
struct InsertColumnBinding
{
    std::string logicalName;
    const model::ColumnView* column;
};

/**
 * Internal mapping from a model to the columns a plain or batch insert binds, in statement order.
 *
 * Auto-increment columns are omitted; a to-one column contributes one logical name per primary-key
 * column of its target ("field_pk") and binds the target column's type. Legacy insert, insertMany
 * and the parameter binders all use this single derivation. This is an implementation seam, not
 * part of the documented backend-extension surface.
 */
[[nodiscard]] inline auto insertColumnBindings(model::ModelView model) -> std::vector<InsertColumnBinding>
{
    std::vector<InsertColumnBinding> bindings;

    for (const auto& column : model->columns)
    {
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto target = model.resolveTarget(column);
            if (target == nullptr)
            {
                throw std::invalid_argument{"To-one column has no target model in the schema"};
            }

            for (const auto& relatedColumn : target->columns)
            {
                if (relatedColumn.isPrimaryKey)
                {
                    bindings.push_back({std::format("{}_{}", column.name, relatedColumn.name), &relatedColumn});
                }
            }
        }
        else if (not column.isAutoIncrement)
        {
            bindings.push_back({std::string{column.name}, &column});
        }
    }

    return bindings;
}
} // namespace detail

class CommandGenerator
{
public:
    virtual ~CommandGenerator() = default;

    [[nodiscard]] virtual auto createTable(model::ModelView model) const -> std::string = 0;
    [[nodiscard]] virtual auto dropTable(model::ModelView model) const -> std::string = 0;
    [[nodiscard]] virtual auto insert(model::ModelView model) const -> std::string = 0;
    /** Non-owning batch statement source; custom generators may keep the default rejection. */
    [[nodiscard]] virtual auto insertMany(model::ModelView /*model*/,
                                          const InsertManySpec& /*spec*/) const -> std::string
    {
        throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, BackendType::Empty, "insert many",
                            "batch inserts are not supported by this command generator"};
    }
    [[nodiscard]] virtual auto select(model::ModelView model, const SelectSpec& spec) const -> SelectStatement = 0;
    [[nodiscard]] virtual auto update(model::ModelView model, const UpdateSpec& spec) const -> Statement = 0;
    [[nodiscard]] virtual auto remove(model::ModelView model, const Predicate& predicate) const -> Statement = 0;
};

class DefaultCommandGenerator : public CommandGenerator
{
public:
    /** The dialect must outlive this generator. */
    explicit DefaultCommandGenerator(const SqlDialect& dialect, BackendType backend = BackendType::Empty);

    [[nodiscard]] auto createTable(model::ModelView model) const -> std::string override;
    [[nodiscard]] auto dropTable(model::ModelView model) const -> std::string override;
    [[nodiscard]] auto insert(model::ModelView model) const -> std::string override;
    [[nodiscard]] auto insertMany(model::ModelView model, const InsertManySpec& spec) const -> std::string override;
    [[nodiscard]] auto select(model::ModelView model, const SelectSpec& spec) const -> SelectStatement override;
    [[nodiscard]] auto update(model::ModelView model, const UpdateSpec& spec) const -> Statement override;
    [[nodiscard]] auto remove(model::ModelView model, const Predicate& predicate) const -> Statement override;

private:
    const SqlDialect& dialect;
    BackendType backend;
};
} // namespace orm::db

namespace orm::db
{
export
{

    class CommandGeneratorFactory
    {
    public:
        CommandGeneratorFactory();
        /** Registers exactly the supplied providers, without adding built-ins. */
        explicit CommandGeneratorFactory(std::vector<std::unique_ptr<BackendProvider>> providers);
        CommandGeneratorFactory(const CommandGeneratorFactory&) = delete;
        CommandGeneratorFactory(CommandGeneratorFactory&&) = default;
        auto operator=(const CommandGeneratorFactory&) -> CommandGeneratorFactory& = delete;
        auto operator=(CommandGeneratorFactory&&) -> CommandGeneratorFactory& = default;

        auto registerBackend(std::unique_ptr<BackendProvider> backend) -> void;
        [[nodiscard]] auto getBackend(BackendType backendType) const -> const BackendProvider&;
        auto getCommandGenerator(BackendType backendType) const -> const CommandGenerator&;

    private:
        std::unordered_map<BackendType, std::unique_ptr<BackendProvider>> backends;
    };
}
} // namespace orm::db
namespace orm::db::relations
{
export
{

    [[nodiscard]] auto createTableStatements(const SqlDialect& dialect, model::ModelView owner,
                                             BackendType backend = BackendType::Empty) -> std::vector<std::string>;
    [[nodiscard]] auto dropTableStatements(const SqlDialect& dialect,
                                           model::ModelView owner) -> std::vector<std::string>;

    [[nodiscard]] auto linkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                     const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey,
                                     BackendType backend = BackendType::Empty) -> Statement;
    [[nodiscard]] auto linkInsertSpec(model::ModelView owner, model::RelationView relation,
                                      const binding::PrimaryKey& ownerKey,
                                      const binding::PrimaryKey& targetKey) -> InsertIfAbsentSpec;
    [[nodiscard]] auto unlinkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                       const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey,
                                       BackendType backend = BackendType::Empty) -> Statement;

    [[nodiscard]] auto collectionSelectStatement(const SqlDialect& dialect, model::ModelView owner,
                                                 model::RelationView relation, std::string targetSelectSql,
                                                 const std::vector<binding::PrimaryKey>& ownerKeys, bool joinedValues,
                                                 BackendType backend = BackendType::Empty) -> Statement;
}
} // namespace orm::db::relations
