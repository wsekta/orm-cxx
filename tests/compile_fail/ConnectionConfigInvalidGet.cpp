import orm;

auto invalidGet(const orm::db::ConnectionConfigView& view) -> void
{
    (void)view.get<int>();
}
