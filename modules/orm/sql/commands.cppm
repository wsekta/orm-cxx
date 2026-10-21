module;

#include <memory>
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

class CommandGenerator
{
public:
    virtual ~CommandGenerator() = default;

    [[nodiscard]] virtual auto createTable(model::ModelView model) const -> std::string = 0;
    [[nodiscard]] virtual auto dropTable(model::ModelView model) const -> std::string = 0;
    [[nodiscard]] virtual auto insert(model::ModelView model) const -> std::string = 0;
    [[nodiscard]] virtual auto select(model::ModelView model, const SelectSpec& spec) const -> SelectStatement = 0;
    [[nodiscard]] virtual auto update(model::ModelView model, const UpdateSpec& spec) const -> Statement = 0;
    [[nodiscard]] virtual auto remove(model::ModelView model, const Predicate& predicate) const -> Statement = 0;
};

class DefaultCommandGenerator : public CommandGenerator
{
public:
    /** The dialect must outlive this generator. */
    explicit DefaultCommandGenerator(const SqlDialect& dialect);

    [[nodiscard]] auto createTable(model::ModelView model) const -> std::string override;
    [[nodiscard]] auto dropTable(model::ModelView model) const -> std::string override;
    [[nodiscard]] auto insert(model::ModelView model) const -> std::string override;
    [[nodiscard]] auto select(model::ModelView model, const SelectSpec& spec) const -> SelectStatement override;
    [[nodiscard]] auto update(model::ModelView model, const UpdateSpec& spec) const -> Statement override;
    [[nodiscard]] auto remove(model::ModelView model, const Predicate& predicate) const -> Statement override;

private:
    const SqlDialect& dialect;
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

    [[nodiscard]] auto createTableStatements(const SqlDialect& dialect,
                                             model::ModelView owner) -> std::vector<std::string>;
    [[nodiscard]] auto dropTableStatements(const SqlDialect& dialect,
                                           model::ModelView owner) -> std::vector<std::string>;

    [[nodiscard]] auto linkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                     const binding::PrimaryKey& ownerKey,
                                     const binding::PrimaryKey& targetKey) -> Statement;
    [[nodiscard]] auto linkInsertSpec(model::ModelView owner, model::RelationView relation,
                                      const binding::PrimaryKey& ownerKey,
                                      const binding::PrimaryKey& targetKey) -> InsertIfAbsentSpec;
    [[nodiscard]] auto unlinkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                       const binding::PrimaryKey& ownerKey,
                                       const binding::PrimaryKey& targetKey) -> Statement;

    [[nodiscard]] auto collectionSelectStatement(
        const SqlDialect& dialect, model::ModelView owner, model::RelationView relation, std::string targetSelectSql,
        const std::vector<binding::PrimaryKey>& ownerKeys, bool joinedValues) -> Statement;
}
} // namespace orm::db::relations
