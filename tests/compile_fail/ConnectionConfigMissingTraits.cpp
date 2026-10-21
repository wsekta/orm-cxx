import orm;

struct MissingTraits
{
};

auto invalidView(MissingTraits& config)
{
    return orm::db::ConnectionConfigView{config};
}
