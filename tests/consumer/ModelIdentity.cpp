import orm;
import orm_cxx_consumer.models;

auto consumerModelTypeId() -> orm::model::TypeId
{
    return orm::model::typeId<consumer_models::ConsumerModel>();
}

auto consumerModelView() -> orm::model::ModelView
{
    return orm::modelView<consumer_models::Schema, consumer_models::ConsumerModel>();
}
