import orm;
import orm_cxx_consumer.models;

// Keep this translation unit free of standard headers: ORM's templates must
// resolve their standard-library operations from their own module interfaces.
struct HeaderlessMappingModel
{
    int key;
    int value;
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&HeaderlessMappingModel::key, "model_key">(),
                         orm::columnName<&HeaderlessMappingModel::value, "model_value">());
    inline static constexpr auto id_columns = orm::primaryKey<&HeaderlessMappingModel::key>();
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&HeaderlessMappingModel::key>();
};

using HeaderlessSchema = orm::Schema<HeaderlessMappingModel>;
constexpr auto headerlessMapping = orm::modelView<HeaderlessSchema, HeaderlessMappingModel>();
static_assert(headerlessMapping->findColumn("model_key")->isPrimaryKey);
static_assert(headerlessMapping->findColumn("key")->isAutoIncrement);
static_assert(headerlessMapping->findRelation("none") == nullptr);
static_assert(headerlessMapping->findRelationField("none") == nullptr);
static_assert(HeaderlessSchema::view.findTable("HeaderlessMappingModel").valid());
static_assert(orm::reflection::FixedString{"key"} == orm::reflection::fieldName<HeaderlessMappingModel, 0>());
static_assert(orm::reflection::FixedString{"key"} == orm::reflection::FixedString{"key"});
static_assert(!(orm::reflection::FixedString{"key"} == orm::reflection::FixedString{"bad"}));
static_assert(!(orm::reflection::FixedString{"key"} == orm::reflection::FixedString{"keys"}));
static_assert(orm::reflection::fieldName<HeaderlessMappingModel, 0>() == orm::reflection::FixedString{"key"});
static_assert(orm::reflection::fields<HeaderlessMappingModel>()[0] ==
              orm::reflection::fields<HeaderlessMappingModel>()[0]);
static_assert(!(orm::reflection::fields<HeaderlessMappingModel>()[0] ==
                orm::reflection::fields<HeaderlessMappingModel>()[1]));

auto consumerModelTypeId() -> orm::model::TypeId
{
    return orm::model::typeId<consumer_models::ConsumerModel>();
}

auto consumerModelView() -> orm::model::ModelView
{
    return orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>();
}
