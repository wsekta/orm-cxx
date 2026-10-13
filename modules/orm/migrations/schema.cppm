module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

export module orm:migrations_schema;

import :foundation;
import :model_views;

export namespace orm::migrations
{
inline constexpr std::array generationBackends{db::BackendType::Sqlite, db::BackendType::Postgres,
                                               db::BackendType::Mysql, db::BackendType::Mariadb};

struct PhysicalColumn
{
    std::string name{};
    model::ColumnType logicalType{model::ColumnType::Int};
    std::string sqlType{};
    bool nullable{};
    std::optional<std::size_t> maxStringLength{};
    bool autoIncrement{};
    auto operator==(const PhysicalColumn&) const -> bool = default;
};
struct PhysicalForeignKey
{
    std::string name{};
    std::vector<std::string> columns{};
    std::string targetTable{};
    std::vector<std::string> targetColumns{};
    std::string onDelete{"NO ACTION"};
    std::string onUpdate{"NO ACTION"};
    auto operator==(const PhysicalForeignKey&) const -> bool = default;
};
struct PhysicalIndex
{
    std::string name{};
    std::vector<std::string> columns{};
    bool unique{};
    auto operator==(const PhysicalIndex&) const -> bool = default;
};
struct PhysicalTable
{
    std::string name{};
    std::vector<PhysicalColumn> columns{};
    std::vector<std::string> primaryKey{};
    std::string primaryKeyName{};
    std::vector<PhysicalForeignKey> foreignKeys{};
    std::vector<PhysicalIndex> indexes{};
    std::string options{};
    auto operator==(const PhysicalTable&) const -> bool = default;
};
struct PhysicalSchema
{
    db::BackendType backend{db::BackendType::Empty};
    std::string profile{};
    std::vector<PhysicalTable> tables{};
    auto operator==(const PhysicalSchema&) const -> bool = default;
};
struct SchemaSnapshot
{
    int formatVersion{1};
    std::map<db::BackendType, PhysicalSchema> backends{};
    auto operator==(const SchemaSnapshot&) const -> bool = default;
};
enum class MigrationExecutionMode
{
    Normal,
    SqliteRebuild,
};
struct RebuildTable
{
    std::string source{};
    std::string target{};
    std::string staging{};
    std::optional<std::string> sourceIdentity{};
    std::optional<std::string> targetIdentity{};
    auto operator==(const RebuildTable&) const -> bool = default;
};
struct MigrationExecutionContext
{
    MigrationExecutionMode mode{MigrationExecutionMode::Normal};
    std::vector<std::string> tables{};
    std::vector<RebuildTable> rebuildTables{};
    std::optional<PhysicalSchema> source{};
    std::optional<PhysicalSchema> target{};
    auto operator==(const MigrationExecutionContext&) const -> bool = default;
};
struct TableRename
{
    std::string from{};
    std::string to{};
};
struct ColumnRename
{
    std::string table{};
    std::string from{};
    std::string to{};
};
using GenerationLiteral = std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string>;
struct ColumnTransformation
{
    std::string table{};
    std::string column{};
    std::optional<GenerationLiteral> constant{};
    std::optional<std::string> sourceColumn{};
    std::map<db::BackendType, std::string> expressions{};
};
struct GenerationDecisions
{
    std::vector<TableRename> tableRenames{};
    std::vector<ColumnRename> columnRenames{};
    std::vector<std::string> allowDataLossUp{};
    std::vector<std::string> allowDataLossDown{};
    std::vector<ColumnTransformation> backfills{};
    std::vector<ColumnTransformation> conversions{};
    std::vector<ColumnTransformation> downBackfills{};
    std::vector<ColumnTransformation> downConversions{};
};
enum class SchemaChangeKind
{
    CreateTable,
    DropTable,
    RenameTable,
    AddColumn,
    DropColumn,
    RenameColumn,
    AlterColumn,
    AlterPrimaryKey,
    AddForeignKey,
    DropForeignKey,
    AddIndex,
    DropIndex,
};
struct SchemaChange
{
    SchemaChangeKind kind{};
    db::BackendType backend{db::BackendType::Empty};
    std::string id{};
    std::string table{};
    std::string column{};
    std::string otherName{};
    std::string description{};
    bool dataLoss{};
    bool unresolved{};
};
struct GenerationDiagnostic
{
    std::string code{};
    std::string message{};
    std::string path{};
    db::BackendType backend{db::BackendType::Empty};
    bool error{true};
};
struct SchemaDiff
{
    SchemaSnapshot before{};
    SchemaSnapshot after{};
    std::vector<SchemaChange> changes{};
    std::vector<GenerationDiagnostic> diagnostics{};
};
class GenerationError final : public std::runtime_error
{
public:
    explicit GenerationError(std::string message, std::vector<GenerationDiagnostic> diagnostics = {});
    [[nodiscard]] auto diagnostics() const noexcept -> const std::vector<GenerationDiagnostic>&;

private:
    std::vector<GenerationDiagnostic> diagnostics_{};
};
}
