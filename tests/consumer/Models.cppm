export module orm_cxx_consumer.models;

import orm;

export namespace consumer_models
{
struct ConsumerModel
{
    int id;
};

struct ConsumerSummary
{
    int id;
};

using Schema = orm::Schema<ConsumerModel>;
}
