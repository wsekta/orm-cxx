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

namespace orm::db::commands
{
export
{

    class CreateTableCommand
    {
    public:
        virtual ~CreateTableCommand() = default;

        [[nodiscard]] virtual auto createTable(model::ModelView model) const -> std::string = 0;
    };
}
} // namespace orm::db

namespace orm::db::commands
{
export
{

    class DeleteCommand
    {
    public:
        virtual ~DeleteCommand() = default;

        [[nodiscard]] virtual auto remove(model::ModelView model, const Predicate& predicate) const -> Statement = 0;
    };
}
} // namespace orm::db::commands

namespace orm::db::commands
{
export
{

    class DropTableCommand
    {
    public:
        virtual ~DropTableCommand() = default;

        [[nodiscard]] virtual auto dropTable(model::ModelView model) const -> std::string = 0;
    };
}
}

namespace orm::db::commands
{
export
{

    class InsertCommand
    {
    public:
        virtual ~InsertCommand() = default;

        [[nodiscard]] virtual auto insert(model::ModelView model) const -> std::string = 0;
    };
}
}

namespace orm::db
{
export
{

    using SelectStatement = Statement;
}
} // namespace orm::db

namespace orm::db::commands
{
export
{

    class SelectCommand
    {
    public:
        virtual ~SelectCommand() = default;

        [[nodiscard]] virtual auto select(model::ModelView model, const SelectSpec& spec) const -> SelectStatement = 0;
    };
}
} // namespace orm::db::commands

namespace orm::db::commands
{
export
{

    class UpdateCommand
    {
    public:
        virtual ~UpdateCommand() = default;

        [[nodiscard]] virtual auto update(model::ModelView model, const UpdateSpec& spec) const -> Statement = 0;
    };
}
} // namespace orm::db::commands

namespace orm::db
{
export
{

    class CommandGenerator
    {
    public:
        CommandGenerator(std::unique_ptr<commands::CreateTableCommand> createTableCommand,
                         std::unique_ptr<commands::DropTableCommand> dropTableCommand,
                         std::unique_ptr<commands::InsertCommand> insertCommand,
                         std::unique_ptr<commands::SelectCommand> selectCommand,
                         std::unique_ptr<commands::UpdateCommand> updateCommand,
                         std::unique_ptr<commands::DeleteCommand> deleteCommand);

        [[nodiscard]] auto createTable(model::ModelView model) const -> std::string;
        [[nodiscard]] auto dropTable(model::ModelView model) const -> std::string;
        [[nodiscard]] auto insert(model::ModelView model) const -> std::string;
        [[nodiscard]] auto select(model::ModelView model, const SelectSpec& spec) const -> SelectStatement;
        [[nodiscard]] auto update(model::ModelView model, const UpdateSpec& spec) const -> Statement;
        [[nodiscard]] auto remove(model::ModelView model, const Predicate& predicate) const -> Statement;

    private:
        std::unique_ptr<commands::CreateTableCommand> createTableCommand;
        std::unique_ptr<commands::DropTableCommand> dropTableCommand;
        std::unique_ptr<commands::InsertCommand> insertCommand;
        std::unique_ptr<commands::SelectCommand> selectCommand;
        std::unique_ptr<commands::UpdateCommand> updateCommand;
        std::unique_ptr<commands::DeleteCommand> deleteCommand;
    };
}
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
        [[nodiscard]] auto findBackend(std::string_view connectionString) const noexcept -> const BackendProvider*;
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
    [[nodiscard]] auto unlinkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                       const binding::PrimaryKey& ownerKey,
                                       const binding::PrimaryKey& targetKey) -> Statement;

    [[nodiscard]] auto collectionSelectStatement(
        const SqlDialect& dialect, model::ModelView owner, model::RelationView relation, std::string targetSelectSql,
        const std::vector<binding::PrimaryKey>& ownerKeys, bool joinedValues) -> Statement;
}
} // namespace orm::db::relations
