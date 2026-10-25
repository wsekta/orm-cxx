module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :migrations_internal;

namespace snapshot_models
{
struct Scalars
{
    int id;
    bool boolean;
    char character;
    unsigned char unsignedCharacter;
    short shortInteger;
    unsigned short unsignedShort;
    unsigned int unsignedInteger;
    long long longInteger;
    unsigned long long unsignedLongInteger;
    float single;
    double real;
    std::string text;
    std::optional<std::string> optionalText;

    inline static constexpr orm::reflection::FixedString table_name{"snapshot_scalars"};
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&Scalars::text, 48>());
};

struct Target
{
    int first;
    std::string second;

    inline static constexpr orm::reflection::FixedString table_name{"snapshot_targets"};
    inline static constexpr auto id_columns = orm::primaryKey<&Target::second, &Target::first>();
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&Target::second, 24>());
};

struct Owner
{
    int id;
    std::optional<Target> target;
    orm::ManyToMany<Target> targets;

    inline static constexpr orm::reflection::FixedString table_name{"snapshot_owners"};
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Owner::targets>()
                                                                .through<"snapshot_owner_targets">()
                                                                .ownerColumns<"owner_id">()
                                                                .targetColumns<"target_second", "target_first">());
};

struct Identity
{
    int id;
    std::string text;
    inline static constexpr orm::reflection::FixedString table_name{"snapshot_identity"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&Identity::id>();
};

struct Reordered
{
    std::string text;
    int id;
    inline static constexpr orm::reflection::FixedString table_name{"snapshot_identity"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&Reordered::id>();
};

struct Keyless
{
    std::optional<std::string> text;
    inline static constexpr orm::reflection::FixedString table_name{"snapshot_keyless"};
    inline static constexpr auto id_columns = orm::primaryKey<>();
};

struct IdentityOwner
{
    int id;
    std::optional<Identity> first;
    std::optional<Identity> second;
    inline static constexpr orm::reflection::FixedString table_name{"snapshot_identity_owners"};
};

struct Quoted
{
    int id;
    std::optional<std::string> text;
    inline static constexpr orm::reflection::FixedString table_name{"a(b\"c"};
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&Quoted::text, "text),comma\"quote">());
};

using Relations = orm::Schema<Owner, Target>;
using All = orm::Schema<Scalars, Owner, Target, Identity>;
}

namespace
{
using namespace orm::migrations;
using orm::db::BackendType;
namespace snapshot_detail = orm::migrations::detail;

auto physical(const SchemaSnapshot& snapshot, BackendType backend, std::string_view name) -> const PhysicalTable&
{
    const auto& tables = snapshot.backends.at(backend).tables;
    return *std::ranges::find(tables, name, &PhysicalTable::name);
}

auto column(const PhysicalTable& table, std::string_view name) -> const PhysicalColumn&
{
    return *std::ranges::find(table.columns, name, &PhysicalColumn::name);
}

auto minimal(BackendType backend = BackendType::Sqlite) -> SchemaSnapshot
{
    auto snapshot = emptySnapshot(std::array{backend});
    snapshot.backends.at(backend).tables.push_back(
        PhysicalTable{.name = "users",
                      .columns = {{.name = "id", .sqlType = backend == BackendType::Mysql ? "INT" : "INTEGER"}},
                      .primaryKey = {"id"},
                      .primaryKeyName = backend == BackendType::Mysql ? "PRIMARY" : "users_pk",
                      .options = backend == BackendType::Mysql ?
                                     "ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin" :
                                     ""});
    return snapshot;
}

template <typename F>
auto invalidMutation(F&& mutate) -> void
{
    auto snapshot = minimal();
    mutate(snapshot, snapshot.backends.at(BackendType::Sqlite).tables.front());
    EXPECT_THROW(snapshot_detail::validateSnapshot(snapshot), GenerationError);
}

struct RuntimeModel
{
    std::vector<orm::model::ColumnView> columns{{.fieldName = "id",
                                                 .name = "id",
                                                 .type = orm::model::ColumnType::Int,
                                                 .isPrimaryKey = true,
                                                 .isNotNull = true,
                                                 .maxStringLength = {}}};
    std::vector<std::size_t> primary{0};
    std::vector<orm::model::RelationView> relations;
    orm::model::ModelDataView data{
        .typeName = {}, .tableName = "runtime_model", .columns = {}, .primaryKeyIndices = {}, .relations = {}};
    std::array<const orm::model::ModelDataView*, 1> models{&data};
    orm::model::SchemaView schema{models};

    auto model() -> orm::model::ModelView
    {
        data.columns = columns;
        data.primaryKeyIndices = primary;
        data.relations = relations;
        return schema.at(0);
    }
};

class FamilyTestDialect final : public orm::tests::TestSqlDialect
{
public:
    auto renderCreateTableSuffix() const -> std::string override
    {
        return "\n) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin;";
    }
};
} // namespace

TEST(SnapshotTest, CapturesAllPhysicalTypesForAllBackendsWithoutConnections)
{
    const auto snapshot = capture(snapshot_models::All::view);
    ASSERT_EQ(snapshot.backends.size(), 4U);
    for (const auto backend : generationBackends)
    {
        const auto& scalar = physical(snapshot, backend, "snapshot_scalars");
        EXPECT_EQ(scalar.columns.size(), 13U);
        EXPECT_EQ(scalar.primaryKey, std::vector<std::string>{"id"});
        EXPECT_FALSE(column(scalar, "text").nullable);
        EXPECT_TRUE(column(scalar, "optionalText").nullable);
        EXPECT_EQ(column(scalar, "text").maxStringLength, 48U);
        EXPECT_EQ(column(scalar, "text").sqlType, backend == BackendType::Sqlite ? "TEXT" : "VARCHAR(48)");
        EXPECT_NE(snapshot.backends.at(backend).profile.find("orm-cxx-ddl-v1:"), std::string::npos);
        EXPECT_NO_THROW(snapshot_detail::validateSnapshot(snapshot));
        for (const auto& table : snapshot.backends.at(backend).tables)
            EXPECT_NE(snapshot_detail::renderCreateTable(backend, table).find("CREATE TABLE"), std::string::npos);
    }
    EXPECT_EQ(column(physical(snapshot, BackendType::Sqlite, "snapshot_scalars"), "unsignedInteger").sqlType,
              "UNSIGNED BIG INT");
    EXPECT_EQ(column(physical(snapshot, BackendType::Postgres, "snapshot_scalars"), "unsignedInteger").sqlType,
              "BIGINT");
    EXPECT_EQ(column(physical(snapshot, BackendType::Mysql, "snapshot_scalars"), "unsignedInteger").sqlType,
              "INT UNSIGNED");
    EXPECT_EQ(physical(snapshot, BackendType::Mysql, "snapshot_scalars").primaryKeyName, "PRIMARY");
    EXPECT_NE(physical(snapshot, BackendType::Mariadb, "snapshot_scalars").options.find("utf8mb4_nopad_bin"),
              std::string::npos);
}

TEST(SnapshotTest, SharesExpansionAndPreservesExplicitPrimaryKeyOrder)
{
    const auto snapshot = capture(snapshot_models::Relations::view);
    for (const auto backend : generationBackends)
    {
        const auto& target = physical(snapshot, backend, "snapshot_targets");
        EXPECT_EQ(target.primaryKey, (std::vector<std::string>{"second", "first"}));
        const auto& owner = physical(snapshot, backend, "snapshot_owners");
        ASSERT_EQ(owner.foreignKeys.size(), 1U);
        EXPECT_EQ(owner.foreignKeys.front().columns, (std::vector<std::string>{"target_second", "target_first"}));
        EXPECT_EQ(owner.foreignKeys.front().targetColumns, target.primaryKey);
        EXPECT_TRUE(column(owner, "target_second").nullable);
        EXPECT_EQ(column(owner, "target_second").maxStringLength, 24U);
        EXPECT_FALSE(column(owner, "target_first").autoIncrement);
        const auto& junction = physical(snapshot, backend, "snapshot_owner_targets");
        EXPECT_EQ(junction.primaryKey, (std::vector<std::string>{"owner_id", "target_second", "target_first"}));
        ASSERT_EQ(junction.foreignKeys.size(), 2U);
        for (const auto& key : junction.foreignKeys)
        {
            EXPECT_EQ(key.onDelete, "CASCADE");
            EXPECT_LT(key.name.size(), 63U);
        }
        for (const auto& physicalColumn : junction.columns)
            EXPECT_FALSE(physicalColumn.nullable);
        const auto sql = snapshot_detail::renderCreateTable(backend, target);
        const auto quote = [backend](std::string_view name) { return snapshot_detail::quoteIdentifier(backend, name); };
        EXPECT_NE(sql.find("PRIMARY KEY (" + quote("second") + ", " + quote("first") + ")"), std::string::npos);
    }
    orm::tests::SnapshotSqlDialect dialect;
    orm::db::DefaultCommandGenerator create{dialect};
    EXPECT_NE(create.createTable(orm::modelView<snapshot_models::Relations, snapshot_models::Target>())
                  .find("PRIMARY KEY (second, first)"),
              std::string::npos);
    const auto junctions = orm::db::relations::createTableStatements(
        dialect, orm::modelView<snapshot_models::Relations, snapshot_models::Owner>());
    ASSERT_EQ(junctions.size(), 1U);
    EXPECT_NE(junctions.front().find("REFERENCES snapshot_targets (second, first)"), std::string::npos);
}

TEST(SnapshotTest, InverseAndOneToManyMappingsDoNotDuplicatePhysicalTables)
{
    const auto snapshot = capture(collection_models::Schema::view);
    const auto& tables = snapshot.backends.at(BackendType::Sqlite).tables;
    EXPECT_EQ(std::ranges::count(tables, "collection_user_roles", &PhysicalTable::name), 1);
    EXPECT_TRUE(snapshot_detail::physicalJunctions(orm::modelView<collection_models::Schema, collection_models::Role>(),
                                                   BackendType::Sqlite)
                    .empty());
    EXPECT_TRUE(snapshot_detail::physicalJunctions(
                    orm::modelView<collection_models::Schema, collection_models::Author>(), BackendType::Sqlite)
                    .empty());
}

TEST(SnapshotTest, SnapshotsOwnDataAndIgnoreModelAndScalarFieldOrder)
{
    const auto left = capture(orm::Schema<snapshot_models::Identity>::view);
    const auto right = capture(orm::Schema<snapshot_models::Reordered>::view);
    EXPECT_EQ(left, right);
    EXPECT_EQ(capture(orm::Schema<snapshot_models::Owner, snapshot_models::Target>::view),
              capture(orm::Schema<snapshot_models::Target, snapshot_models::Owner>::view));
    SchemaSnapshot captured;
    {
        RuntimeModel runtime;
        const auto model = runtime.model();
        static_cast<void>(model);
        captured = capture(runtime.schema, std::array{BackendType::Sqlite});
        runtime.columns.front().name = "changed";
    }
    EXPECT_EQ(captured.backends.at(BackendType::Sqlite).tables.front().columns.front().name, "id");
}

TEST(SnapshotTest, SupportsEmptySchemasSelectedBackendsAndKeylessTables)
{
    const std::array backends{BackendType::Sqlite, BackendType::Mariadb};
    EXPECT_EQ(capture(orm::Schema<>::view, backends), emptySnapshot(backends));
    const auto snapshot = capture(orm::Schema<snapshot_models::Keyless>::view, backends);
    ASSERT_EQ(snapshot.backends.size(), 2U);
    for (const auto backend : backends)
    {
        const auto& table = physical(snapshot, backend, "snapshot_keyless");
        EXPECT_TRUE(table.primaryKey.empty());
        EXPECT_TRUE(table.primaryKeyName.empty());
        EXPECT_TRUE(table.columns.front().nullable);
        EXPECT_EQ(snapshot_detail::renderCreateTable(backend, table).find("PRIMARY KEY"), std::string::npos);
    }
}

TEST(SnapshotTest, ForeignKeysInheritIdentityTypeWithoutBecomingIdentityColumns)
{
    const auto snapshot = capture(orm::Schema<snapshot_models::IdentityOwner, snapshot_models::Identity>::view);
    for (const auto backend : generationBackends)
    {
        const auto& owner = physical(snapshot, backend, "snapshot_identity_owners");
        ASSERT_EQ(owner.foreignKeys.size(), 2U);
        EXPECT_NE(owner.foreignKeys[0].name, owner.foreignKeys[1].name);
        EXPECT_EQ(column(owner, "first_id").sqlType,
                  column(physical(snapshot, backend, "snapshot_identity"), "id").sqlType);
        EXPECT_FALSE(column(owner, "first_id").autoIncrement);
        EXPECT_FALSE(column(owner, "second_id").autoIncrement);
        EXPECT_TRUE(column(owner, "first_id").nullable);
    }
}

TEST(SnapshotTest, FamilyForeignKeysHaveExplicitManagedSupportingIndexes)
{
    const auto snapshot = capture(snapshot_models::Relations::view);
    for (const auto backend : generationBackends)
    {
        const auto& owner = physical(snapshot, backend, "snapshot_owners");
        const auto& junction = physical(snapshot, backend, "snapshot_owner_targets");
        if (backend == BackendType::Sqlite || backend == BackendType::Postgres)
        {
            EXPECT_TRUE(owner.indexes.empty());
            EXPECT_TRUE(junction.indexes.empty());
            continue;
        }
        ASSERT_EQ(owner.indexes.size(), 1U);
        ASSERT_EQ(junction.indexes.size(), 1U);
        EXPECT_TRUE(owner.indexes.front().name.starts_with("orm_fkidx_"));
        EXPECT_FALSE(owner.indexes.front().unique);
        EXPECT_EQ(owner.indexes.front().columns, (std::vector<std::string>{"target_second", "target_first"}));
        // The owner endpoint uses the junction PK prefix; the target needs its
        // own declared index. Neither may depend on InnoDB's implicit names.
        EXPECT_EQ(junction.indexes.front().columns, owner.indexes.front().columns);
        const auto withoutKeys = snapshot_detail::renderCreateTable(backend, owner, false);
        EXPECT_NE(withoutKeys.find("INDEX "), std::string::npos);
        EXPECT_EQ(withoutKeys.find("FOREIGN KEY"), std::string::npos);
        const auto withKeys = snapshot_detail::renderCreateTable(backend, owner);
        EXPECT_LT(withKeys.find("INDEX "), withKeys.find("FOREIGN KEY"));
    }
    FamilyTestDialect dialect;
    const auto model = orm::modelView<snapshot_models::Relations, snapshot_models::Owner>();
    const auto owner = snapshot_detail::physicalTable(model, dialect);
    ASSERT_EQ(owner.indexes.size(), 1U);
    EXPECT_NE(snapshot_detail::renderCreateTable(dialect, owner, false).find("INDEX "), std::string::npos);
    const auto junctions = snapshot_detail::physicalJunctions(model, dialect);
    ASSERT_EQ(junctions.size(), 1U);
    EXPECT_EQ(junctions.front().indexes.size(), 1U);
}

TEST(SnapshotTest, FamilyOverlappingForeignKeysShareAnExplicitPrefixIndex)
{
    for (const auto backend : {BackendType::Mysql, BackendType::Mariadb})
    {
        auto snapshot = capture(snapshot_models::Relations::view, std::array{backend});
        auto& tables = snapshot.backends.at(backend).tables;
        auto& owner = *std::ranges::find(tables, "snapshot_owners", &PhysicalTable::name);
        auto& target = *std::ranges::find(tables, "snapshot_targets", &PhysicalTable::name);
        target.indexes.push_back({.name = "target_second_unique", .columns = {"second"}, .unique = true});
        owner.foreignKeys.push_back({.name = "short_fk",
                                     .columns = {"target_second"},
                                     .targetTable = "snapshot_targets",
                                     .targetColumns = {"second"}});
        snapshot_detail::canonicalize(snapshot);
        ASSERT_EQ(owner.indexes.size(), 1U);
        EXPECT_NO_THROW(snapshot_detail::validateSnapshot(snapshot));
        const auto sql = snapshot_detail::renderCreateTable(backend, owner);
        EXPECT_LT(sql.find("INDEX "), sql.find("FOREIGN KEY"));
        owner.indexes.clear();
        EXPECT_THROW(snapshot_detail::validateSnapshot(snapshot), GenerationError);
        owner.indexes.push_back({.name = "wrong_order", .columns = {"target_first", "target_second"}});
        EXPECT_THROW(snapshot_detail::validateSnapshot(snapshot), GenerationError);
    }
}

TEST(SnapshotTest, RendersQuotedPhysicalNamesWithoutTreatingTheirContentsAsSql)
{
    const auto snapshot = capture(orm::Schema<snapshot_models::Quoted>::view, std::array{BackendType::Sqlite});
    const auto& table = physical(snapshot, BackendType::Sqlite, "a(b\"c");
    const auto sql = snapshot_detail::renderCreateTable(BackendType::Sqlite, table);
    EXPECT_NE(sql.find("CREATE TABLE \"a(b\"\"c\" ("), std::string::npos);
    EXPECT_NE(sql.find("\"text),comma\"\"quote\" TEXT"), std::string::npos);
    EXPECT_NO_THROW(snapshot_detail::validateSnapshot(snapshot));
}

TEST(SnapshotTest, QuotesIdentifiersAndUsesFrozenSqlTypes)
{
    EXPECT_EQ(snapshot_detail::quoteIdentifier(BackendType::Sqlite, "has\"quote"), "\"has\"\"quote\"");
    EXPECT_EQ(snapshot_detail::quoteIdentifier(BackendType::Mysql, "has`quote"), "`has``quote`");
    EXPECT_THROW((void)snapshot_detail::quoteIdentifier(BackendType::Empty, "x"), GenerationError);
    EXPECT_THROW((void)snapshot_detail::quoteIdentifier(BackendType::Sqlite, ""), GenerationError);
    EXPECT_THROW((void)snapshot_detail::quoteIdentifier(BackendType::Sqlite, std::string{"a\0b", 3}), GenerationError);
    auto snapshot = minimal();
    auto& table = snapshot.backends.at(BackendType::Sqlite).tables.front();
    table.columns.front().sqlType = "BIGINT";
    table.indexes.push_back({.name = "users_idx", .columns = {"id"}});
    EXPECT_NO_THROW(snapshot_detail::validateSnapshot(snapshot));
    const auto sql = snapshot_detail::renderCreateTable(BackendType::Sqlite, table, false, true);
    EXPECT_NE(sql.find("IF NOT EXISTS"), std::string::npos);
    EXPECT_NE(sql.find("\"id\" BIGINT NOT NULL"), std::string::npos);
    EXPECT_EQ(sql.find("INDEX"), std::string::npos);
    table.columns.front().sqlType = "INT; DROP TABLE users";
    EXPECT_THROW((void)snapshot_detail::renderCreateTable(BackendType::Sqlite, table), GenerationError);
}

TEST(SnapshotTest, FrozenTypeCannotInjectColumnsOrUnmodeledConstraints)
{
    FamilyTestDialect dialect;
    for (const std::string_view type :
         {"INTEGER, injected INTEGER", "INTEGER DEFAULT 7", "INTEGER CHECK (0)", "INTEGER REFERENCES other(id)",
          "INTEGER NOT NULL", "INTEGER PRIMARY KEY", "INTEGER UNIQUE", "INTEGER COLLATE NOCASE",
          "INTEGER GENERATED ALWAYS AS (0)", "INTEGER AUTO_INCREMENT", "CHECK(0)", "DEFAULT(7)", "AS(7)", "UNIQUE",
          "PRIMARY KEY", "VARCHAR(foo)", "DECIMAL(10,2,3)", "INT(10)UNSIGNED", "INTEGER((7))"})
    {
        SCOPED_TRACE(type);
        auto snapshot = minimal();
        auto& table = snapshot.backends.at(BackendType::Sqlite).tables.front();
        table.columns.front().sqlType = std::string{type};
        EXPECT_THROW(snapshot_detail::validateSnapshot(snapshot), GenerationError);
        EXPECT_THROW((void)snapshot_detail::renderCreateTable(dialect, table), GenerationError);
        for (const auto backend : generationBackends)
        {
            EXPECT_THROW((void)snapshot_detail::renderColumn(backend, table.columns.front()), GenerationError);
            EXPECT_THROW((void)snapshot_detail::renderCreateTable(backend, table), GenerationError);
        }
    }
}

TEST(SnapshotTest, FrozenTypeAllowsNumericParametersCanonicalNamesAndSingleCustomTypes)
{
    FamilyTestDialect dialect;
    for (const std::string_view type :
         {"INTEGER", "DECIMAL(10,2)", "DECIMAL(10, 2)", "NUMERIC(20,0)", "VARCHAR(32)", "DOUBLE PRECISION",
          "UNSIGNED BIG INT", "TINYINT UNSIGNED", "BIGINT UNSIGNED", "DECIMAL(10,2) UNSIGNED", "INT(10) SIGNED",
          "CHARACTER VARYING(32)", "custom_type7", "  decimal ( 10 , 2 ) unsigned  "})
    {
        SCOPED_TRACE(type);
        auto snapshot = minimal();
        auto& table = snapshot.backends.at(BackendType::Sqlite).tables.front();
        table.columns.front().sqlType = std::string{type};
        EXPECT_NO_THROW(snapshot_detail::validateSnapshot(snapshot));
        EXPECT_NO_THROW((void)snapshot_detail::renderCreateTable(dialect, table));
        for (const auto backend : generationBackends)
        {
            EXPECT_NO_THROW((void)snapshot_detail::renderColumn(backend, table.columns.front()));
            EXPECT_NO_THROW((void)snapshot_detail::renderCreateTable(backend, table));
        }
    }
}

TEST(SnapshotTest, RendersIdentityLengthAndForeignKeyActions)
{
    const auto captured = capture(orm::Schema<snapshot_models::Identity>::view);
    for (const auto backend : generationBackends)
    {
        const auto& table = physical(captured, backend, "snapshot_identity");
        const auto sql = snapshot_detail::renderCreateTable(backend, table);
        EXPECT_NE(sql.find(backend == BackendType::Sqlite   ? "AUTOINCREMENT" :
                           backend == BackendType::Postgres ? "GENERATED BY DEFAULT AS IDENTITY" :
                                                              "AUTO_INCREMENT"),
                  std::string::npos);
        EXPECT_EQ(column(table, "id").sqlType,
                  backend == BackendType::Mysql || backend == BackendType::Mariadb ? "INT" : "INTEGER");
    }
    PhysicalColumn bounded{.name = "text",
                           .logicalType = orm::model::ColumnType::String,
                           .sqlType = "TEXT",
                           .nullable = true,
                           .maxStringLength = 5};
    EXPECT_NE(snapshot_detail::renderColumn(BackendType::Sqlite, bounded).find("length(replace("), std::string::npos);
    PhysicalForeignKey key{.name = "fk",
                           .columns = {"owner"},
                           .targetTable = "other",
                           .targetColumns = {"id"},
                           .onDelete = "SET NULL",
                           .onUpdate = "CASCADE"};
    const auto foreignKey = snapshot_detail::renderForeignKey(BackendType::Postgres, key);
    EXPECT_NE(foreignKey.find("ON DELETE SET NULL ON UPDATE CASCADE"), std::string::npos);
    key.onDelete = "RESTRICT";
    EXPECT_NE(snapshot_detail::renderForeignKey(BackendType::Postgres, key).find("ON DELETE RESTRICT"),
              std::string::npos);
    key.columns.clear();
    EXPECT_THROW((void)snapshot_detail::renderForeignKey(BackendType::Sqlite, key), GenerationError);
    auto identity = column(physical(captured, BackendType::Sqlite, "snapshot_identity"), "id");
    EXPECT_THROW((void)snapshot_detail::renderColumn(BackendType::Sqlite, identity), GenerationError);
    identity.nullable = true;
    EXPECT_THROW((void)snapshot_detail::renderColumn(BackendType::Postgres, identity), GenerationError);
    auto table = physical(captured, BackendType::Postgres, "snapshot_identity");
    table.primaryKey.clear();
    EXPECT_THROW((void)snapshot_detail::renderCreateTable(BackendType::Postgres, table), GenerationError);
    table.columns.clear();
    EXPECT_THROW((void)snapshot_detail::renderCreateTable(BackendType::Postgres, table), GenerationError);
}

TEST(SnapshotTest, ValidatesBackendsFormatsAndProfiles)
{
    EXPECT_THROW((void)emptySnapshot(std::span<const BackendType>{}), GenerationError);
    EXPECT_THROW((void)emptySnapshot(std::array{BackendType::Sqlite, BackendType::Sqlite}), GenerationError);
    EXPECT_THROW((void)emptySnapshot(std::array{BackendType::Oracle}), GenerationError);
    invalidMutation([](auto& snapshot, auto&) { snapshot.formatVersion = 2; });
    invalidMutation([](auto& snapshot, auto&)
                    { snapshot.backends.at(BackendType::Sqlite).backend = BackendType::Mysql; });
    invalidMutation([](auto& snapshot, auto&) { snapshot.backends.at(BackendType::Sqlite).profile = "unknown"; });
    try
    {
        auto snapshot = minimal();
        snapshot.formatVersion = 99;
        snapshot_detail::validateSnapshot(snapshot);
        FAIL() << "Expected invalid snapshot";
    }
    catch (const GenerationError& error)
    {
        ASSERT_EQ(error.diagnostics().size(), 1U);
        EXPECT_EQ(error.diagnostics().front().code, "invalid-snapshot");
        EXPECT_FALSE(error.diagnostics().front().message.empty());
    }
}

TEST(SnapshotTest, ValidatesTableAndColumnShapes)
{
    invalidMutation([](auto&, auto& table) { table.name.clear(); });
    invalidMutation([](auto&, auto& table) { table.name = "_orm_migrations"; });
    invalidMutation([](auto&, auto& table) { table.name = "_orm_migrations_incomplete"; });
    invalidMutation([](auto&, auto& table) { table.name = "sqlite_reserved"; });
    invalidMutation([](auto&, auto& table) { table.name = "__orm_rebuild_0123456789abcdef"; });
    invalidMutation([](auto& snapshot, auto& table)
                    { snapshot.backends.at(BackendType::Sqlite).tables.push_back(table); });
    invalidMutation([](auto&, auto& table) { table.columns.clear(); });
    invalidMutation([](auto&, auto& table) { table.options = "ENGINE=Unexpected"; });
    invalidMutation([](auto&, auto& table) { table.columns.push_back(table.columns.front()); });
    invalidMutation([](auto&, auto& table) { table.columns.front().name = std::string{"x\0y", 3}; });
    invalidMutation([](auto&, auto& table) { table.columns.front().name = "__orm_column_0123456789abcdef"; });
    invalidMutation(
        [](auto&, auto& table)
        {
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            table.columns.front().logicalType = static_cast<orm::model::ColumnType>(-1);
        });
    for (const auto& type : {"", "INT;DELETE", "VARCHAR(", "INT)", "INT\n"})
        invalidMutation([&](auto&, auto& table) { table.columns.front().sqlType = type; });
    invalidMutation([](auto&, auto& table) { table.columns.front().maxStringLength = 5; });
    invalidMutation(
        [](auto&, auto& table)
        {
            table.columns.front().logicalType = orm::model::ColumnType::String;
            table.columns.front().maxStringLength = 0;
        });
    auto pg = minimal(BackendType::Postgres);
    pg.backends.at(BackendType::Postgres).tables.front().name = std::string(64, 'x');
    EXPECT_THROW(snapshot_detail::validateSnapshot(pg), GenerationError);
    auto mysql = minimal(BackendType::Mysql);
    auto& string = mysql.backends.at(BackendType::Mysql).tables.front().columns.front();
    string.logicalType = orm::model::ColumnType::String;
    string.sqlType = "VARCHAR(16384)";
    string.maxStringLength = 16'384;
    EXPECT_THROW(snapshot_detail::validateSnapshot(mysql), GenerationError);
}

TEST(SnapshotTest, ValidatesPrimaryKeysIndexesAndIdentity)
{
    invalidMutation([](auto&, auto& table) { table.primaryKey.push_back("id"); });
    invalidMutation([](auto&, auto& table) { table.primaryKey = {"missing"}; });
    invalidMutation([](auto&, auto& table) { table.columns.front().nullable = true; });
    invalidMutation([](auto&, auto& table) { table.primaryKeyName.clear(); });
    invalidMutation([](auto&, auto& table) { table.primaryKey.clear(); });
    invalidMutation(
        [](auto&, auto& table)
        {
            table.columns.front().autoIncrement = true;
            table.columns.front().nullable = true;
        });
    invalidMutation(
        [](auto&, auto& table)
        {
            table.columns.front().autoIncrement = true;
            table.columns.front().logicalType = orm::model::ColumnType::LongLong;
        });
    invalidMutation(
        [](auto&, auto& table)
        {
            table.columns.front().autoIncrement = true;
            table.primaryKey.clear();
            table.primaryKeyName.clear();
        });
    invalidMutation([](auto&, auto& table) { table.indexes.push_back({.name = "index", .columns = {}}); });
    invalidMutation([](auto&, auto& table) { table.indexes.push_back({.name = "index", .columns = {"missing"}}); });
    invalidMutation([](auto&, auto& table)
                    { table.indexes.push_back({.name = table.primaryKeyName, .columns = {"id"}}); });
    auto snapshot = minimal(BackendType::Mysql);
    snapshot.backends.at(BackendType::Mysql).tables.front().primaryKeyName = "wrong";
    EXPECT_THROW(snapshot_detail::validateSnapshot(snapshot), GenerationError);
}

TEST(SnapshotTest, ValidatesForeignKeyReferencesTypesActionsAndUniqueTargets)
{
    const auto invalidKey = [](const auto& mutate)
    {
        auto snapshot = minimal();
        auto& tables = snapshot.backends.at(BackendType::Sqlite).tables;
        tables.push_back(PhysicalTable{.name = "owners", .columns = {{.name = "user_id", .sqlType = "INTEGER"}}});
        auto& owner = tables.back();
        owner.foreignKeys.push_back(
            {.name = "owners_fk", .columns = {"user_id"}, .targetTable = "users", .targetColumns = {"id"}});
        mutate(snapshot, owner, owner.foreignKeys.front());
        EXPECT_THROW(snapshot_detail::validateSnapshot(snapshot), GenerationError);
    };
    invalidKey([](auto&, auto&, auto& key) { key.name.clear(); });
    invalidKey([](auto&, auto&, auto& key) { key.targetTable = "missing"; });
    invalidKey([](auto&, auto&, auto& key) { key.targetColumns = {"missing"}; });
    invalidKey([](auto&, auto&, auto& key) { key.targetColumns.clear(); });
    invalidKey([](auto&, auto&, auto& key) { key.onDelete = "TRUNCATE"; });
    invalidKey([](auto&, auto& owner, auto&) { owner.foreignKeys.push_back(owner.foreignKeys.front()); });
    invalidKey([](auto&, auto& owner, auto&) { owner.columns.front().sqlType = "BIGINT"; });
    invalidKey([](auto&, auto&, auto& key) { key.onDelete = "SET NULL"; });
    invalidKey(
        [](auto& snapshot, auto&, auto&)
        {
            auto& target = snapshot.backends.at(BackendType::Sqlite).tables.front();
            target.primaryKey.clear();
            target.primaryKeyName.clear();
        });
    auto snapshot = minimal();
    auto& target = snapshot.backends.at(BackendType::Sqlite).tables.front();
    target.primaryKey.clear();
    target.primaryKeyName.clear();
    target.indexes.push_back({.name = "users_unique", .columns = {"id"}, .unique = true});
    snapshot.backends.at(BackendType::Sqlite)
        .tables.push_back({.name = "owner",
                           .columns = {{.name = "user_id", .sqlType = "INTEGER", .nullable = true}},
                           .foreignKeys = {{.name = "owner_fk",
                                            .columns = {"user_id"},
                                            .targetTable = "users",
                                            .targetColumns = {"id"},
                                            .onDelete = "SET NULL",
                                            .onUpdate = "CASCADE"}}});
    EXPECT_NO_THROW(snapshot_detail::validateSnapshot(snapshot));
}

TEST(SnapshotTest, RejectsInvalidRuntimeModelDescriptorsAndUnsupportedTypes)
{
    EXPECT_THROW((void)snapshot_detail::physicalTable({}, BackendType::Sqlite), GenerationError);
    EXPECT_THROW((void)snapshot_detail::physicalJunctions({}, BackendType::Sqlite), GenerationError);
    RuntimeModel model;
    model.primary = {10};
    EXPECT_THROW((void)snapshot_detail::physicalTable(model.model(), BackendType::Sqlite), GenerationError);
    model.primary.clear();
    model.columns.front().type.reset();
    EXPECT_THROW((void)snapshot_detail::physicalTable(model.model(), BackendType::Sqlite), GenerationError);
    model.columns.front().targetModelIndex = 0;
    EXPECT_THROW((void)snapshot_detail::physicalTable(model.model(), BackendType::Sqlite), GenerationError);
    model.columns.front().kind = orm::model::FieldKind::ToOne;
    EXPECT_THROW((void)snapshot_detail::physicalTable(model.model(), BackendType::Sqlite), GenerationError);
    model.columns.front().kind = orm::model::FieldKind::Scalar;
    model.columns.front().type = orm::model::ColumnType::Uuid;
    for (const auto backend : {BackendType::Sqlite, BackendType::Mysql})
        EXPECT_THROW((void)snapshot_detail::physicalTable(model.model(), backend), GenerationError);
    for (const auto backend : {BackendType::Postgres, BackendType::Mariadb})
        EXPECT_EQ(snapshot_detail::physicalTable(model.model(), backend).columns.front().sqlType, "UUID");
    model.columns.front().type = orm::model::ColumnType::String;
    model.columns.front().maxStringLength = 0;
    EXPECT_THROW((void)snapshot_detail::physicalTable(model.model(), BackendType::Sqlite), GenerationError);
    model.columns.front().maxStringLength = 16'384;
    EXPECT_THROW((void)snapshot_detail::physicalTable(model.model(), BackendType::Mysql), GenerationError);
    model.columns.front().type = orm::model::ColumnType::Int;
    model.columns.front().maxStringLength = 5;
    EXPECT_THROW((void)snapshot_detail::physicalTable(model.model(), BackendType::Postgres), GenerationError);
    model.columns.front().maxStringLength.reset();
    model.relations = {
        {.fieldName = {},
         .columnName = {},
         .kind = orm::model::RelationKind::ManyToMany,
         .mappedBy = {},
         .junction = {.tableName = "bad_junction", .ownerColumns = {}, .targetColumns = {}, .owningSide = true}}};
    EXPECT_THROW((void)snapshot_detail::physicalJunctions(model.model(), BackendType::Sqlite), GenerationError);
    model.relations.front().targetModelIndex = 0;
    model.primary = {0};
    EXPECT_THROW((void)snapshot_detail::physicalJunctions(model.model(), BackendType::Sqlite), GenerationError);
}

namespace
{
auto allowInverseLoss(const SchemaSnapshot& before, const SchemaSnapshot& after) -> GenerationDecisions
{
    GenerationDecisions decisions;
    for (const auto& change : compare(after, before).changes)
        if (change.dataLoss)
            decisions.allowDataLossDown.push_back(change.id);
    return decisions;
}

auto withNullableLabel(const SchemaSnapshot& before) -> SchemaSnapshot
{
    auto result = before;
    for (auto& [backend, schema] : result.backends)
    {
        auto& identity = *std::ranges::find(schema.tables, "snapshot_identity", &PhysicalTable::name);
        identity.columns.push_back(
            {.name = "label",
             .logicalType = orm::model::ColumnType::String,
             .sqlType = backend == BackendType::Mysql || backend == BackendType::Mariadb ? "LONGTEXT" : "TEXT",
             .nullable = true});
    }
    snapshot_detail::canonicalize(result);
    return result;
}
} // namespace

class SnapshotMigrationTest : public DatabaseTest<snapshot_models::All>
{
};

TEST_P(SnapshotMigrationTest, GeneratedModelSchemasExecuteAndPreserveRowsAcrossUpAndDown)
{
    const auto backend = GetParam().type;
    const auto empty = emptySnapshot(std::array{backend});
    const auto before = capture(snapshot_models::All::view, std::array{backend});
    const auto initial = generate(1, "initial", compare(empty, before), allowInverseLoss(empty, before)).migration;
    Runner{connection, Catalog{initial}}.up();
    auto& session = snapshot_detail::DatabaseAccess::session(connection);
    const auto quote = [backend](std::string_view name) { return snapshot_detail::quoteIdentifier(backend, name); };
    session << "INSERT INTO " + quote("snapshot_identity") + " (" + quote("text") + ") VALUES ('first')";
    const auto after = withNullableLabel(before);
    const auto second = generate(2, "label", compare(before, after), allowInverseLoss(before, after)).migration;
    Runner runner{connection, Catalog{initial, second}};
    ASSERT_NO_THROW(runner.up());
    EXPECT_EQ(runner.status().currentVersion, 2);
    int count{};
    session << "SELECT COUNT(*) FROM " + quote("snapshot_identity"), soci::into(count);
    EXPECT_EQ(count, 1);
    ASSERT_NO_THROW(runner.down(1));
    int id{};
    session << "SELECT " + quote("id") + " FROM " + quote("snapshot_identity"), soci::into(id);
    EXPECT_EQ(id, 1);
    ASSERT_NO_THROW(runner.down(0));
    EXPECT_EQ(runner.status().currentVersion, 0);
}

TEST_P(SnapshotMigrationTest, SharedCreateTableDdlMatchesTheGeneratedSnapshot)
{
    createTable<snapshot_models::Scalars>();
    createTable<snapshot_models::Target>();
    createTable<snapshot_models::Owner>();
    createTable<snapshot_models::Identity>();
    createRelationTables<snapshot_models::Owner>();
    const auto backend = GetParam().type;
    const auto empty = emptySnapshot(std::array{backend});
    const auto before = capture(snapshot_models::All::view, std::array{backend});
    const auto initial = generate(1, "initial", compare(empty, before), allowInverseLoss(empty, before)).migration;
    Runner{connection, Catalog{initial}}.baseline(1);
    auto& session = snapshot_detail::DatabaseAccess::session(connection);
    const auto quote = [backend](std::string_view name) { return snapshot_detail::quoteIdentifier(backend, name); };
    session << "INSERT INTO " + quote("snapshot_identity") + " (" + quote("text") + ") VALUES ('first')";
    const auto after = withNullableLabel(before);
    const auto second = generate(2, "label", compare(before, after), allowInverseLoss(before, after)).migration;
    Runner runner{connection, Catalog{initial, second}};
    ASSERT_NO_THROW(runner.up());
    ASSERT_NO_THROW(runner.down(1));
    EXPECT_EQ(runner.status().baselineVersion, 1);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, SnapshotMigrationTest, orm::test::fixtures::conformanceBackendTestConfigs,
                         orm::test::fixtures::backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(SnapshotMigrationTest);
