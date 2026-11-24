module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :config;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::test::fixtures;

namespace insert_many_parity_models
{
struct AutoIdRecord
{
    int id;
    int field1;
    std::string field2;

    inline static constexpr orm::reflection::FixedString table_name{"insert_many_parity_auto_id"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoIdRecord::id>();
};

struct UuidRecord
{
    int id;
    orm::Uuid value;

    inline static constexpr orm::reflection::FixedString table_name{"insert_many_parity_uuid"};
};

struct TextRecord
{
    int id;
    std::string text;
    std::int64_t big;

    inline static constexpr orm::reflection::FixedString table_name{"insert_many_parity_text"};
};

struct AutoOnlyRecord
{
    int id;

    inline static constexpr orm::reflection::FixedString table_name{"insert_many_parity_auto_only"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoOnlyRecord::id>();
};

// Deliberately not part of InsertManyParitySchema: the model/schema gate must reject it.
struct ForeignModel
{
    int id;
};
} // namespace insert_many_parity_models

using InsertManyParitySchema =
    orm::Schema<models::ModelWithId, models::ModelRelatedToOtherModel, models::ModelWithOptional,
                insert_many_parity_models::AutoIdRecord, insert_many_parity_models::UuidRecord,
                insert_many_parity_models::TextRecord, insert_many_parity_models::AutoOnlyRecord>;

namespace
{
auto makeModelWithIdRows(int count, int firstId = 1) -> std::vector<models::ModelWithId>
{
    std::vector<models::ModelWithId> rows;
    rows.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index)
    {
        rows.push_back(models::ModelWithId{firstId + index, index, std::format("row-{}", index)});
    }

    return rows;
}

auto selectAllModelWithId(orm::OrmContext<InsertManyParitySchema>& database) -> std::vector<models::ModelWithId>
{
    orm::Query<models::ModelWithId> query;
    query.orderBy(orm::query::asc(orm::query::col<&models::ModelWithId::id>()));
    return database.select(query);
}

template <typename T>
auto selectAllOrderedById(orm::OrmContext<InsertManyParitySchema>& database) -> std::vector<T>
{
    orm::Query<T> query;
    query.orderBy(orm::query::asc(orm::query::col<&T::id>()));
    return database.select(query);
}

auto countRows(orm::OrmContext<InsertManyParitySchema>& database, auto modelTag) -> std::size_t
{
    using Model = decltype(modelTag);
    orm::Query<Model> query;
    return database.select(query).size();
}

template <typename Operation>
auto expectInsertManyError(Operation operation, orm::DatabaseErrorCode code) -> void
{
    try
    {
        operation();
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), code);
        EXPECT_EQ(error.getOperation(), "insert many");
    }
}

auto postgresqlCommandGenerator() -> const orm::db::CommandGenerator&
{
    static const orm::db::CommandGeneratorFactory factory;
    return factory.getBackend(orm::db::BackendType::Postgres).commandGenerator();
}

auto sqliteParamConfigs() -> std::vector<BackendTestConfig>
{
    std::vector<BackendTestConfig> configs;
    if constexpr (orm::config::sqliteBackendEnabled)
    {
        configs.push_back(sqliteBackendTestConfig);
    }
    return configs;
}

} // namespace

class InsertManyParityTest : public DatabaseTest<InsertManyParitySchema>
{
};

class InsertManyParitySqliteTest : public DatabaseTest<InsertManyParitySchema>
{
};

TEST_P(InsertManyParityTest, vectorInputWritesEveryRow)
{
    createTable<models::ModelWithId>();
    const auto rows = makeModelWithIdRows(3);

    database.insertMany(rows);

    const auto stored = selectAllModelWithId(database);
    ASSERT_EQ(stored.size(), rows.size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].field1, rows[index].field1);
        EXPECT_EQ(stored[index].field2, rows[index].field2);
    }
}

TEST_P(InsertManyParityTest, explicitSpanOverArrayWritesEveryRow)
{
    createTable<models::ModelWithId>();
    const std::array<models::ModelWithId, 3> rows{
        models::ModelWithId{1, 10, "first"}, models::ModelWithId{2, 20, "second"}, models::ModelWithId{3, 30, "third"}};

    database.insertMany(std::span<const models::ModelWithId>{rows});

    const auto stored = selectAllModelWithId(database);
    ASSERT_EQ(stored.size(), rows.size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].field1, rows[index].field1);
        EXPECT_EQ(stored[index].field2, rows[index].field2);
    }
}

TEST_P(InsertManyParityTest, emptySpanAndEmptyVectorPerformNoBackendWork)
{
    const std::vector<models::ModelWithId> emptyRows;
    EXPECT_NO_THROW(database.insertMany(emptyRows));
    EXPECT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{}));
}

TEST_P(InsertManyParityTest, foreignModelTypeStaysOutsideTheClosedSchema)
{
    // insertMany instantiates requireSchemaModel<T>(), so a foreign model is a compile-time
    // rejection (ORM_SCHEMA_MODEL_MISSING, proven by tests/compile_fail/InsertManyMissingModel.cpp).
    static_assert(not InsertManyParitySchema::contains<insert_many_parity_models::ForeignModel>);
    EXPECT_FALSE(InsertManyParitySchema::contains<insert_many_parity_models::ForeignModel>);
    EXPECT_TRUE(InsertManyParitySchema::contains<models::ModelWithId>);
}

TEST_P(InsertManyParityTest, autoIncrementIdIsOmittedFromSqlAndGeneratedByTheDatabase)
{
    createTable<insert_many_parity_models::AutoIdRecord>();
    const std::vector<insert_many_parity_models::AutoIdRecord> rows{
        {0, 10, "first"},
        {0, 20, "second"},
        {0, 30, "third"},
    };

    const auto sql = postgresqlCommandGenerator().insertMany(
        orm::modelView<InsertManyParitySchema, insert_many_parity_models::AutoIdRecord>(),
        orm::db::InsertManySpec{.rowCount = 2});
    EXPECT_EQ(sql.find("\"id\""), std::string::npos) << sql;
    EXPECT_NE(sql.find("\"field1\""), std::string::npos) << sql;
    EXPECT_NE(sql.find("\"field2\""), std::string::npos) << sql;
    EXPECT_EQ(sql.find(orm::db::insertManyParameterName(1, "id")), std::string::npos) << sql;

    database.insertMany(rows);

    const auto stored = selectAllOrderedById<insert_many_parity_models::AutoIdRecord>(database);
    ASSERT_EQ(stored.size(), rows.size());

    // Generated ids and physical row order are not part of the public contract; compare the
    // stored field1 -> field2 mapping and check only that every generated key is nonzero and unique.
    std::map<int, std::string> storedByField1;
    std::unordered_set<int> generatedIds;

    for (const auto& row : stored)
    {
        EXPECT_NE(row.id, 0);
        EXPECT_TRUE(generatedIds.insert(row.id).second) << "generated id must be unique: " << row.id;
        EXPECT_TRUE(storedByField1.emplace(row.field1, row.field2).second)
            << "field1 must identify exactly one stored row: " << row.field1;
    }

    EXPECT_EQ(generatedIds.size(), rows.size());
    EXPECT_EQ(storedByField1.size(), rows.size());

    for (const auto& row : rows)
    {
        const auto found = storedByField1.find(row.field1);
        ASSERT_NE(found, storedByField1.end()) << "stored mapping is missing field1 " << row.field1;
        EXPECT_EQ(found->second, row.field2);
    }
}

TEST_P(InsertManyParityTest, toOneForeignKeyColumnsAreBoundAndVerified)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();

    database.insert(models::ModelWithId{1, 10, "target-one"});
    database.insert(models::ModelWithId{2, 20, "target-two"});

    std::vector<models::ModelRelatedToOtherModel> rows;
    rows.reserve(33);
    for (int index = 0; index < 33; ++index)
    {
        const int targetId = index % 2 == 0 ? 1 : 2;
        rows.push_back(models::ModelRelatedToOtherModel{index + 1, index, std::format("parent-{}", index),
                                                        models::ModelWithId{targetId, 0, std::string{}}});
    }

    database.insertMany(rows);

    const auto stored = selectAllOrderedById<models::ModelRelatedToOtherModel>(database);
    ASSERT_EQ(stored.size(), rows.size());

    std::map<int, int> expectedTargetByParentId;
    for (const auto& row : rows)
    {
        expectedTargetByParentId.emplace(row.id, row.field3.id);
    }

    for (const auto& row : stored)
    {
        const auto expectedTarget = expectedTargetByParentId.find(row.id);
        ASSERT_NE(expectedTarget, expectedTargetByParentId.end());
        EXPECT_EQ(row.field3.id, expectedTarget->second);
        EXPECT_EQ(row.field3.field2, expectedTarget->second == 1 ? "target-one" : "target-two");
    }

    EXPECT_EQ(countRows(database, models::ModelWithId{}), 2);
}

TEST_P(InsertManyParityTest, foreignKeyViolationInLaterChunkLeavesNoPartialRows)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    database.insert(models::ModelWithId{1, 10, "target-one"});

    std::vector<models::ModelRelatedToOtherModel> rows;
    rows.reserve(33);
    for (int index = 0; index < 33; ++index)
    {
        const int targetId = index == 32 ? 99 : 1;
        rows.push_back(models::ModelRelatedToOtherModel{index + 1, index, std::format("parent-{}", index),
                                                        models::ModelWithId{targetId, 0, std::string{}}});
    }

    try
    {
        database.insertMany(rows);
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
        EXPECT_EQ(error.getOperation(), "insert many");
        ASSERT_TRUE(error.getNativeCode().has_value());
        EXPECT_EQ(error.getNativeCode().value(), "23503");
    }

    EXPECT_EQ(countRows(database, models::ModelRelatedToOtherModel{}), 0);
}

TEST_P(InsertManyParityTest, oversizedStringRowIsRejectedBeforeAnyWrite)
{
    createTable<insert_many_parity_models::TextRecord>();
    const std::string oversized(4 * 1024 * 1024 + 1, 'x');
    const std::vector<insert_many_parity_models::TextRecord> rows{{1, oversized, 7}};

    expectInsertManyError([this, &rows]() { database.insertMany(rows); }, orm::DatabaseErrorCode::UnsupportedFeature);

    EXPECT_EQ(countRows(database, insert_many_parity_models::TextRecord{}), 0);
}

TEST_P(InsertManyParityTest, oversizedRowAfterAnEarlierChunkLeavesNoCommittedRows)
{
    createTable<insert_many_parity_models::TextRecord>();
    const std::string oversized(4 * 1024 * 1024 + 1, 'x');

    std::vector<insert_many_parity_models::TextRecord> rows;
    rows.reserve(33);
    for (int index = 0; index < 32; ++index)
    {
        rows.push_back({index + 1, std::format("chunk-{}", index), index});
    }
    rows.push_back({33, oversized, 33});

    // The row budget of 32 commits no partial work: the first chunk runs, then the oversized row
    // aborts the batch and the operation rolls back its own transaction.
    expectInsertManyError([this, &rows]() { database.insertMany(rows); }, orm::DatabaseErrorCode::UnsupportedFeature);

    EXPECT_EQ(countRows(database, insert_many_parity_models::TextRecord{}), 0);
}

TEST_P(InsertManyParityTest, nativeUuidColumnRoundTrips)
{
    createTable<insert_many_parity_models::UuidRecord>();
    const auto createSql = postgresqlCommandGenerator().createTable(
        orm::modelView<InsertManyParitySchema, insert_many_parity_models::UuidRecord>());
    EXPECT_NE(createSql.find("UUID"), std::string::npos) << createSql;

    const auto first = orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff");
    const auto second = orm::Uuid::parse("ffeeddcc-bbaa-9988-7766-554433221100");
    const std::vector<insert_many_parity_models::UuidRecord> rows{
        {1, first},
        {2, second},
    };

    database.insertMany(rows);

    const auto stored = selectAllOrderedById<insert_many_parity_models::UuidRecord>(database);
    ASSERT_EQ(stored.size(), 2);
    EXPECT_EQ(stored[0].value, first);
    EXPECT_EQ(stored[1].value, second);
    EXPECT_NE(stored[0].value, stored[1].value);
}

TEST_P(InsertManyParityTest, nullableColumnKeepsNullDistinctFromEmptyString)
{
    createTable<models::ModelWithOptional>();
    const std::vector<models::ModelWithOptional> rows{
        models::ModelWithOptional{std::nullopt, std::nullopt, std::nullopt},
        models::ModelWithOptional{1, std::string{}, 0.0},
    };

    database.insertMany(rows);

    orm::Query<models::ModelWithOptional> nullQuery;
    nullQuery.where(orm::query::col<&models::ModelWithOptional::field2>().isNull());
    const auto nullRows = database.select(nullQuery);
    ASSERT_EQ(nullRows.size(), 1);
    EXPECT_FALSE(nullRows[0].field1.has_value());
    EXPECT_FALSE(nullRows[0].field2.has_value());

    orm::Query<models::ModelWithOptional> emptyQuery;
    emptyQuery.where(orm::query::col<&models::ModelWithOptional::field2>() == std::string{});
    const auto emptyRows = database.select(emptyQuery);
    ASSERT_EQ(emptyRows.size(), 1);
    EXPECT_EQ(emptyRows[0].field1, std::optional<int>{1});
    EXPECT_EQ(emptyRows[0].field2, std::optional<std::string>{std::string{}});
}

TEST_P(InsertManyParityTest, apostropheUnicodeAndEmptyTextRoundTrip)
{
    createTable<insert_many_parity_models::TextRecord>();
    const std::string apostrophe{"O'Brien said 'insertMany' works"};
    const std::string unicode{"Zażółć gęślą jaźń — 漢字 — 🚀"};
    const std::vector<insert_many_parity_models::TextRecord> rows{
        {1, apostrophe, 1},
        {2, unicode, 2},
        {3, std::string{}, 3},
    };

    database.insertMany(rows);

    const auto stored = selectAllOrderedById<insert_many_parity_models::TextRecord>(database);
    ASSERT_EQ(stored.size(), 3);
    EXPECT_EQ(stored[0].text, apostrophe);
    EXPECT_EQ(stored[1].text, unicode);
    EXPECT_TRUE(stored[2].text.empty());
}

TEST_P(InsertManyParityTest, int64BoundariesRoundTrip)
{
    createTable<insert_many_parity_models::TextRecord>();
    const std::vector<insert_many_parity_models::TextRecord> rows{
        {1, "min", std::numeric_limits<std::int64_t>::min()},
        {2, "zero", 0},
        {3, "max", std::numeric_limits<std::int64_t>::max()},
    };

    database.insertMany(rows);

    const auto stored = selectAllOrderedById<insert_many_parity_models::TextRecord>(database);
    ASSERT_EQ(stored.size(), 3);
    EXPECT_EQ(stored[0].big, std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(stored[1].big, 0);
    EXPECT_EQ(stored[2].big, std::numeric_limits<std::int64_t>::max());
}

TEST_P(InsertManyParityTest, largeTextRoundTrips)
{
    createTable<insert_many_parity_models::TextRecord>();
    std::string largeText(100'000, 'x');
    largeText.replace(50'000, 6, " środek ");
    largeText.replace(99'990, 10, "O'Brien's;");

    const std::vector<insert_many_parity_models::TextRecord> rows{{1, largeText, 7}};

    database.insertMany(rows);

    const auto stored = selectAllOrderedById<insert_many_parity_models::TextRecord>(database);
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored[0].text.size(), largeText.size());
    EXPECT_EQ(stored[0].text, largeText);
    EXPECT_EQ(stored[0].big, 7);
}

TEST_P(InsertManyParityTest, modelWithoutBoundColumnsIsRejectedBeforeAnyWrite)
{
    createTable<insert_many_parity_models::AutoOnlyRecord>();
    const std::array<insert_many_parity_models::AutoOnlyRecord, 2> rows{insert_many_parity_models::AutoOnlyRecord{0},
                                                                        insert_many_parity_models::AutoOnlyRecord{0}};

    expectInsertManyError([this, &rows]()
                          { database.insertMany(std::span<const insert_many_parity_models::AutoOnlyRecord>{rows}); },
                          orm::DatabaseErrorCode::UnsupportedFeature);

    EXPECT_EQ(countRows(database, insert_many_parity_models::AutoOnlyRecord{}), 0);
}

TEST_P(InsertManyParityTest, plainInsertSingleAndVectorKeepTheirContracts)
{
    createTable<models::ModelWithId>();

    EXPECT_NO_THROW(database.insert(models::ModelWithId{1, 10, "single"}));

    const std::vector<models::ModelWithId> rows{
        models::ModelWithId{2, 20, "written-before-failure"},
        models::ModelWithId{1, 30, "duplicate"},
    };

    EXPECT_THROW(database.insert(rows), orm::DatabaseError);

    // The legacy vector insert stays sequential and non-atomic: the first row is kept.
    const auto stored = selectAllModelWithId(database);
    ASSERT_EQ(stored.size(), 2);
    EXPECT_EQ(stored[0].id, 1);
    EXPECT_EQ(stored[1].id, 2);
    EXPECT_EQ(stored[1].field2, "written-before-failure");
}

TEST_P(InsertManyParityTest, temporaryVectorLivesForTheBorrowedCall)
{
    createTable<models::ModelWithId>();

    database.insertMany(std::vector<models::ModelWithId>{models::ModelWithId{1, 10, "temp-first"},
                                                         models::ModelWithId{2, 20, "temp-second"}});

    const auto stored = selectAllModelWithId(database);
    ASSERT_EQ(stored.size(), 2);
    EXPECT_EQ(stored[0].field2, "temp-first");
    EXPECT_EQ(stored[1].field2, "temp-second");
}

TEST_P(InsertManyParitySqliteTest, capabilityStaysDisabledAndEmptyInputIsANoOp)
{
    EXPECT_FALSE(connection.getBackendCapabilities().mutations.insertMany);

    const std::vector<models::ModelWithId> emptyRows;
    EXPECT_NO_THROW(database.insertMany(emptyRows));
    EXPECT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{}));
}

TEST_P(InsertManyParitySqliteTest, nonEmptyInputIsRejectedBeforeAnyWrite)
{
    createTable<models::ModelWithId>();
    const auto rows = makeModelWithIdRows(3);

    expectInsertManyError([this, &rows]() { database.insertMany(std::span<const models::ModelWithId>{rows}); },
                          orm::DatabaseErrorCode::UnsupportedFeature);

    EXPECT_EQ(countRows(database, models::ModelWithId{}), 0);
}

INSTANTIATE_TEST_SUITE_P(Postgresql, InsertManyParityTest, ::testing::Values(postgresqlBackendTestConfig),
                         backendTestName);
// With the SQLite backend disabled this instantiation expands to zero values. GoogleTest would
// otherwise register a failing "never instantiated" verification case in PostgreSQL-only presets.
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(InsertManyParitySqliteTest);
INSTANTIATE_TEST_SUITE_P(Sqlite, InsertManyParitySqliteTest, ::testing::ValuesIn(sqliteParamConfigs()),
                         backendTestName);
