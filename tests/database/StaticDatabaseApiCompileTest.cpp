#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "orm-cxx/database.hpp"

namespace
{
struct Department
{
    int id{};
    std::string name;
};

struct Employee
{
    int id{};
    Department department;
    std::optional<int> rank;
};

struct EmployeeName
{
    std::string name;
};

using ApplicationSchema = orm::Schema<Department, Employee>;
using ApplicationDatabase = orm::Database<ApplicationSchema>;

static_assert(ApplicationSchema::contains<Department>);
static_assert(ApplicationSchema::contains<Employee>);
static_assert(not ApplicationSchema::contains<EmployeeName>);
static_assert(std::is_default_constructible_v<ApplicationDatabase>);
static_assert(std::same_as<ApplicationDatabase::Payload<Employee>,
                           orm::db::binding::BindingPayload<Employee, ApplicationSchema>>);

// This function is intentionally not executed. Compiling it instantiates the
// public schema-bound facade and both SOCI conversion directions.
[[maybe_unused]] auto instantiateSchemaBoundApi(ApplicationDatabase& database, soci::values& values) -> void
{
    orm::Query<Employee> query;
    (void)database.select(query);

    orm::ProjectionQuery<Employee, EmployeeName> projection;
    projection.project(orm::query::as("name", orm::query::col("name")));
    (void)database.select(projection);

    database.insert(Employee{});
    database.insert(std::vector<Employee>{});

    orm::Update<Employee> update;
    update.set(orm::query::col("rank"), 2).where(orm::query::col("id") == 1);
    (void)database.update(update);
    (void)database.remove<Employee>(orm::query::col("id") == 1);
    database.createTable<Employee>();
    database.deleteTable<Employee>();

    using Payload = ApplicationDatabase::Payload<Employee>;
    Payload payload{};
    auto indicator = soci::i_ok;
    soci::type_conversion<Payload>::to_base(payload, values, indicator);
    soci::type_conversion<Payload>::from_base(values, indicator, payload);
}
} // namespace
