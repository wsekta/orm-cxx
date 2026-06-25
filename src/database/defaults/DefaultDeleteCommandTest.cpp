#include "DefaultDeleteCommand.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <variant>

#include "orm-cxx/model.hpp"
#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"

using namespace orm::query;

namespace
{
template <typename T>
auto getValue(const orm::db::StatementParameter& parameter) -> T
{
    return std::get<T>(parameter.value->get());
}
} // namespace

class DefaultDeleteCommandTest : public ::testing::Test
{
public:
    orm::db::commands::DefaultDeleteCommand command;
};

TEST_F(DefaultDeleteCommandTest, removeWithComparisonPredicate)
{
    orm::Model<models::ModelWithFloat> model;

    const auto statement = command.remove(model.getModelInfo(), col("field1") == 5);

    EXPECT_EQ(statement.sql, "DELETE FROM models_ModelWithFloat WHERE models_ModelWithFloat.field1 = :orm_p0;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(statement.parameters[0].name, "orm_p0");
    EXPECT_EQ(getValue<int>(statement.parameters[0]), 5);
}

TEST_F(DefaultDeleteCommandTest, removeWithMappedFieldName)
{
    orm::Model<models::ModelWithIdAndNamesMapping> model;

    EXPECT_EQ(command.remove(model.getModelInfo(), col("field1") == 7).sql,
              "DELETE FROM models_ModelWithIdAndNamesMapping WHERE "
              "models_ModelWithIdAndNamesMapping.some_field1_name = :orm_p0;");
}

TEST_F(DefaultDeleteCommandTest, removeWithRawPredicate)
{
    orm::Model<models::ModelWithFloat> model;

    const auto statement = command.remove(model.getModelInfo(), raw("field2 = :name", param("name", "target")));

    EXPECT_EQ(statement.sql, "DELETE FROM models_ModelWithFloat WHERE field2 = :name;");
    ASSERT_EQ(statement.parameters.size(), 1);
    EXPECT_EQ(statement.parameters[0].name, "name");
    EXPECT_EQ(getValue<std::string>(statement.parameters[0]), "target");
}

TEST_F(DefaultDeleteCommandTest, removeWithRelatedPrimaryKeyPredicate)
{
    orm::Model<models::ModelRelatedToOtherModel> model;

    EXPECT_EQ(command.remove(model.getModelInfo(), col("field3.id") == 2).sql,
              "DELETE FROM models_ModelRelatedToOtherModel WHERE "
              "models_ModelRelatedToOtherModel.field3_id = :orm_p0;");
}

TEST_F(DefaultDeleteCommandTest, removeWithRelatedNonPrimaryKeyPredicate_shouldThrow)
{
    orm::Model<models::ModelRelatedToOtherModel> model;

    EXPECT_THROW((void)command.remove(model.getModelInfo(), col("field3.field2") == "target"), std::invalid_argument);
}

TEST_F(DefaultDeleteCommandTest, removeWithUnknownColumn_shouldThrow)
{
    orm::Model<models::ModelWithFloat> model;

    EXPECT_THROW((void)command.remove(model.getModelInfo(), col("missing") == 1), std::invalid_argument);
}
