#include "DefaultUpdateCommand.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "orm-cxx/update.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/FakeDatabase.hpp"

using namespace orm::query;

namespace
{
template <typename T>
auto getValue(const orm::db::StatementParameter& parameter) -> T
{
    return std::get<T>(parameter.value->get());
}
} // namespace

class DefaultUpdateCommandTest : public ::testing::Test
{
public:
    orm::db::commands::DefaultUpdateCommand command;
};

TEST_F(DefaultUpdateCommandTest, updateWithComparisonPredicate)
{
    orm::Update<models::ModelWithFloat> update;

    update.set(col("field2"), "updated").where(col("field1") == 5);

    const auto statement = command.update(orm::Database::getUpdateData(update));

    EXPECT_EQ(statement.sql,
              "UPDATE models_ModelWithFloat SET field2 = :orm_p0 WHERE models_ModelWithFloat.field1 = :orm_p1;");
    ASSERT_EQ(statement.parameters.size(), 2);
    EXPECT_EQ(statement.parameters[0].name, "orm_p0");
    EXPECT_EQ(getValue<std::string>(statement.parameters[0]), "updated");
    EXPECT_EQ(statement.parameters[1].name, "orm_p1");
    EXPECT_EQ(getValue<int>(statement.parameters[1]), 5);
}

TEST_F(DefaultUpdateCommandTest, updateWithNullAssignment)
{
    orm::Update<models::ModelWithOptional> update;

    update.set(col("field1"), std::nullopt).where(col("field2") == "target");

    const auto statement = command.update(orm::Database::getUpdateData(update));

    EXPECT_EQ(statement.sql,
              "UPDATE models_ModelWithOptional SET field1 = :orm_p0 WHERE models_ModelWithOptional.field2 = :orm_p1;");
    ASSERT_EQ(statement.parameters.size(), 2);
    EXPECT_EQ(statement.parameters[0].name, "orm_p0");
    EXPECT_FALSE(statement.parameters[0].value.has_value());
    EXPECT_EQ(statement.parameters[0].nullType, orm::model::ColumnType::Int);
    EXPECT_EQ(getValue<std::string>(statement.parameters[1]), "target");
}

TEST_F(DefaultUpdateCommandTest, updateWithNullAssignmentToNotNullColumn_shouldThrow)
{
    orm::Update<models::ModelWithFloat> update;

    update.set(col("field2"), std::nullopt).where(col("field1") == 5);

    EXPECT_THROW((void)command.update(orm::Database::getUpdateData(update)), std::invalid_argument);
}

TEST_F(DefaultUpdateCommandTest, updateWithMappedFieldName)
{
    orm::Update<models::ModelWithIdAndNamesMapping> update;

    update.set(col("field2"), "updated").where(col("field1") == 7);

    EXPECT_EQ(command.update(orm::Database::getUpdateData(update)).sql,
              "UPDATE models_ModelWithIdAndNamesMapping SET some_field2_name = :orm_p0 WHERE "
              "models_ModelWithIdAndNamesMapping.some_field1_name = :orm_p1;");
}

TEST_F(DefaultUpdateCommandTest, updateWithPrimaryKeyAssignment)
{
    orm::Update<models::ModelWithId> update;

    update.set(col("id"), 9).where(col("field1") == 1);

    EXPECT_EQ(command.update(orm::Database::getUpdateData(update)).sql,
              "UPDATE models_ModelWithId SET id = :orm_p0 WHERE models_ModelWithId.field1 = :orm_p1;");
}

TEST_F(DefaultUpdateCommandTest, updateWithRelatedPrimaryKeyAssignment)
{
    orm::Update<models::ModelRelatedToOtherModel> update;

    update.set(col("field3.id"), 2).where(col("id") == 1);

    EXPECT_EQ(command.update(orm::Database::getUpdateData(update)).sql,
              "UPDATE models_ModelRelatedToOtherModel SET field3_id = :orm_p0 WHERE "
              "models_ModelRelatedToOtherModel.id = :orm_p1;");
}

TEST_F(DefaultUpdateCommandTest, updateWithRelatedNonPrimaryKeyAssignment_shouldThrow)
{
    orm::Update<models::ModelRelatedToOtherModel> update;

    update.set(col("field3.field2"), "updated").where(col("id") == 1);

    EXPECT_THROW((void)command.update(orm::Database::getUpdateData(update)), std::invalid_argument);
}

TEST_F(DefaultUpdateCommandTest, updateWithoutPredicate_shouldThrow)
{
    orm::Update<models::ModelWithFloat> update;

    update.set(col("field2"), "updated");

    EXPECT_THROW((void)command.update(orm::Database::getUpdateData(update)), std::invalid_argument);
}

TEST_F(DefaultUpdateCommandTest, updateWithoutAssignments_shouldThrow)
{
    orm::Update<models::ModelWithFloat> update;

    update.where(col("field1") == 1);

    EXPECT_THROW((void)command.update(orm::Database::getUpdateData(update)), std::invalid_argument);
}

TEST_F(DefaultUpdateCommandTest, updateWithUnknownColumn_shouldThrow)
{
    orm::Update<models::ModelWithFloat> update;

    update.set(col("missing"), 1).where(col("field1") == 1);

    EXPECT_THROW((void)command.update(orm::Database::getUpdateData(update)), std::invalid_argument);
}

TEST_F(DefaultUpdateCommandTest, updateWithRawPredicate)
{
    orm::Update<models::ModelWithFloat> update;

    update.set(col("field2"), "updated")
        .where(raw("models_ModelWithFloat.field1 = :value", param("value", 1)));

    const auto statement = command.update(orm::Database::getUpdateData(update));

    EXPECT_EQ(statement.sql,
              "UPDATE models_ModelWithFloat SET field2 = :orm_p0 WHERE models_ModelWithFloat.field1 = :value;");
    ASSERT_EQ(statement.parameters.size(), 2);
    EXPECT_EQ(statement.parameters[0].name, "orm_p0");
    EXPECT_EQ(statement.parameters[1].name, "value");
}
