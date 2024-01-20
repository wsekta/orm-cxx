#include "orm-cxx/model.hpp"

#include <gtest/gtest.h>

#include "ModelsDefinitions.hpp"

TEST(ModelTest, OneFieldStruct_shouldHaveOneColumn)
{
    orm::Model<models::ModelWithOneField> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 1);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].type, orm::model::ColumnType::Int);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isNotNull, true);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.empty());
}

TEST(ModelTest, StructWithTableName_shouldHaveTwoColumns)
{
    orm::Model<models::ModelWithTableName> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 1);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "field1");
    EXPECT_EQ(model.getModelInfo().tableName, models::ModelWithTableName::table_name);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.empty());
}

TEST(ModelTest, StructWithOptional_shouldHaveTwoColumns)
{
    orm::Model<models::ModelWithOptional> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 3);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].type, orm::model::ColumnType::Int);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isNotNull, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].name, "field2");
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].isNotNull, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].name, "field3");
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].isNotNull, false);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.empty());
}

TEST(ModelTest, StructWithId_shouldHaveOneIdColumn)
{
    orm::Model<models::ModelWithId> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 3);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "id");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isPrimaryKey, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].isPrimaryKey, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].name, "field2");
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].isPrimaryKey, false);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("id"));
}

TEST(ModelTest, StructWithIdColumns_shouldHaveTwoIdColumns)
{
    orm::Model<models::ModelWithOverwrittenId> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 3);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "id");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isPrimaryKey, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].isPrimaryKey, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].name, "field2");
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].isPrimaryKey, true);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("field1"));
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("field2"));
}

TEST(ModelTest, StructWithNamesMapping_shouldHaveTwoColumnsWithNamesFromMapping)
{
    orm::Model<models::ModelWithIdAndNamesMapping> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 3);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "some_id_name");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isPrimaryKey, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].name, "some_field1_name");
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].name, "some_field2_name");
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("some_id_name"));
}

TEST(ModelTest, StructWithOtherStructWithId_shouldHaveProperlyFilledForeignIdsInfo)
{
    orm::Model<models::ModelRelatedToOtherModel> model;
    orm::Model<models::ModelWithId> relatedModel;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 4);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "id");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isPrimaryKey, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isForeignModel, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].name, "field3");
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].isPrimaryKey, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].isForeignModel, true);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("id"));
    EXPECT_TRUE(model.getModelInfo().foreignModelsInfo.contains("field3"));
    EXPECT_EQ(model.getModelInfo().foreignModelsInfo["field3"].idColumnsNames.size(), 1);
    EXPECT_TRUE(model.getModelInfo().foreignModelsInfo["field3"].idColumnsNames.contains("id"));
    EXPECT_EQ(model.getModelInfo().foreignModelsInfo["field3"].tableName, relatedModel.getModelInfo().tableName);
}

TEST(ModelTest, StructWithOptionalOtherStructWithId_shouldHaveProperlyFilledForeignIdsInfo)
{
    orm::Model<models::ModelOptionallyRelatedToOtherModel> model;
    orm::Model<models::ModelWithId> relatedModel;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 4);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "id");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isPrimaryKey, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isForeignModel, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].name, "field3");
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].isPrimaryKey, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].isForeignModel, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].isNotNull, false);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("id"));
    EXPECT_TRUE(model.getModelInfo().foreignModelsInfo.contains("field3"));
    EXPECT_EQ(model.getModelInfo().foreignModelsInfo["field3"].idColumnsNames.size(), 1);
    EXPECT_TRUE(model.getModelInfo().foreignModelsInfo["field3"].idColumnsNames.contains("id"));
    EXPECT_EQ(model.getModelInfo().foreignModelsInfo["field3"].tableName, relatedModel.getModelInfo().tableName);
}
