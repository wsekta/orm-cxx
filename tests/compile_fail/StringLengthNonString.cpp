#include <string>
import orm;
struct Other
{
    std::string value;
};
struct Record
{
    int id;
    int value;
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&Record::value, 3>());
};
using Schema = orm::Schema<Record>;
constexpr auto descriptor = orm::modelView<Schema, Record>();
