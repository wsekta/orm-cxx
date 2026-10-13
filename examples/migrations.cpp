#include <optional>
#include <string>

import orm;

struct MigrationUser
{
    int id;
    std::string name;
    std::optional<std::string> email;
    inline static constexpr orm::reflection::FixedString table_name{"migration_example_users"};
};

int main(int argc, char** argv)
{
    using ApplicationSchema = orm::Schema<MigrationUser>;
    return orm::migrations::runCommandLine(argc, argv, ApplicationSchema::view);
}
