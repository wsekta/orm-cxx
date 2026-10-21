import orm;

auto invalidView()
{
    return orm::db::ConnectionConfigView{orm::PostgresConfig{}};
}
