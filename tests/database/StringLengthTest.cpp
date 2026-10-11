module;
#include "tests/UnitTestPrelude.hpp"
module orm;
import :internal;
import :test_support;
import :database;
import :static_plan;

using namespace orm::test::fixtures;
namespace bounded_models
{
struct Record
{
    int id;
    std::string value;
    std::optional<std::string> note;
    inline static constexpr auto string_lengths =
        orm::stringLengths(orm::stringLength<&Record::value, 3>(), orm::stringLength<&Record::note, 3>());
};
struct MissingBound
{
    std::string id;
};
struct OversizedKey
{
    std::string id;
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&OversizedKey::id, 1000>());
};
struct TextParent
{
    std::string id;
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&TextParent::id, 2>());
};
struct TextChild
{
    int id;
    std::optional<TextParent> parent;
};
struct UnboundedRecord
{
    int id;
    std::string value;
};
struct OverwideOrdering
{
    int id;
    std::string value;
    inline static constexpr auto string_lengths =
        orm::stringLengths(orm::stringLength<&OverwideOrdering::value, 20000>());
};
using Schema =
    orm::Schema<Record, MissingBound, OversizedKey, TextParent, TextChild, UnboundedRecord, OverwideOrdering>;
}
class StringLengthDatabaseTest : public DatabaseTest<bounded_models::Schema>
{
};

TEST_P(StringLengthDatabaseTest, InsertAndBothUpdatePathsEnforceTheSameCharacterLimit)
{
    using bounded_models::Record;
    using namespace orm::query;
    createTable<Record>();
    database.insert(Record{1, "a\xc4\x85\xf0\x9f\x98\x80", {}});
    EXPECT_THROW(database.insert(Record{2, "four", {}}), orm::DatabaseError);
    constexpr auto patch = update<Record>()
                               .set(col<&Record::value>(), param<std::string, 0>())
                               .where(col<&Record::id>() == param<int, 1>());
    EXPECT_THROW(database.update(patch, std::string{"four"}, 1), orm::DatabaseError);
    EXPECT_EQ(database.update(patch, std::string{"yes"}, 1), 1);
    auto dynamic = patch.toDynamic(std::string{"four"}, 1);
    EXPECT_THROW(database.update(dynamic), orm::DatabaseError);
    auto nullPatch = update<Record>()
                         .set(col<&Record::note>(), param<std::optional<std::string>, 0>())
                         .where(col<&Record::id>() == param<int, 1>());
    EXPECT_EQ(database.update(nullPatch, std::optional<std::string>{}, 1), 1);
    EXPECT_THROW(database.update(nullPatch, std::optional<std::string>{"four"}, 1), orm::DatabaseError);
}

TEST_P(StringLengthDatabaseTest, TextualKeysAndIndexBudgetAreValidatedBeforeDdl)
{
    if (not connection.getBackendCapabilities().schema.boundedStringKeys)
        GTEST_SKIP();
    EXPECT_THROW(database.createTable<bounded_models::MissingBound>(), orm::DatabaseError);
    EXPECT_THROW(database.createTable<bounded_models::OversizedKey>(), orm::DatabaseError);
}

TEST_P(StringLengthDatabaseTest, NontransactionalDdlIsRejectedWithoutCommittingApplicationWrites)
{
    using bounded_models::Record;
    if (connection.getBackendCapabilities().schema.transactionalDdl)
        GTEST_SKIP();
    createTable<Record>();
    connection.beginTransaction();
    database.insert(Record{1, "one", {}});
    EXPECT_THROW(database.deleteTable<Record>(), orm::DatabaseError);
    connection.rollbackTransaction();
    orm::Query<Record> query;
    EXPECT_TRUE(database.select(query).empty());
}
TEST_P(StringLengthDatabaseTest, ForeignKeyWritesPreserveTargetStringLimits)
{
    using bounded_models::TextChild;
    using bounded_models::TextParent;
    createTable<TextParent>();
    createTable<TextChild>();
    database.insert(TextParent{"\xc4\x85\xf0\x9f\x98\x80"});
    database.insert(TextChild{1, TextParent{"\xc4\x85\xf0\x9f\x98\x80"}});
    try
    {
        database.insert(TextChild{2, TextParent{"abc"}});
        FAIL() << "Expected bounded foreign key rejection";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Conversion);
    }
}

TEST_P(StringLengthDatabaseTest, StringOrderingChecksBoundsOnStaticAndDynamicQueries)
{
    using bounded_models::OverwideOrdering;
    using bounded_models::Record;
    using bounded_models::UnboundedRecord;
    using namespace orm::query;
    createTable<Record>();
    database.insert(Record{1, "two", {}});
    database.insert(Record{2, "one", {}});
    constexpr auto bounded = select<Record>().orderBy(asc(col<&Record::value>()));
    EXPECT_EQ(database.select(bounded).front().id, 2);
    auto dynamicBounded = bounded.toDynamic();
    EXPECT_EQ(database.select(dynamicBounded).front().id, 2);
    createTable<UnboundedRecord>();
    database.insert(UnboundedRecord{1, std::string(1100, 'x') + 'b'});
    database.insert(UnboundedRecord{2, std::string(1100, 'x') + 'a'});
    constexpr auto unbounded = select<UnboundedRecord>().orderBy(asc(col<&UnboundedRecord::value>()));
    auto dynamicUnbounded = unbounded.toDynamic();
    if (connection.getBackendCapabilities().query.maxOrderedStringLength)
    {
        for (auto operation : {0, 1})
            try
            {
                if (operation == 0)
                    (void)database.select(unbounded);
                else
                    (void)database.select(dynamicUnbounded);
                FAIL() << "Expected rejection before truncated string sorting";
            }
            catch (const orm::DatabaseError& error)
            {
                EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
            }
        constexpr auto oversized = select<OverwideOrdering>().orderBy(asc(col<&OverwideOrdering::value>()));
        EXPECT_THROW(database.select(oversized), orm::DatabaseError);
    }
    else
    {
        EXPECT_EQ(database.select(unbounded).front().id, 2);
        EXPECT_EQ(database.select(dynamicUnbounded).front().id, 2);
    }
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, StringLengthDatabaseTest, backendTestConfigs, backendTestName);
