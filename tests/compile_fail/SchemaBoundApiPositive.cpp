#include <soci/soci.h>

#include "tests/StandardLibrary.hpp"

import orm;

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
using ApplicationContext = orm::OrmContext<ApplicationSchema>;

static_assert(ApplicationSchema::contains<Department>);
static_assert(ApplicationSchema::contains<Employee>);
static_assert(not ApplicationSchema::contains<EmployeeName>);
static_assert(not std::is_default_constructible_v<ApplicationContext>);
static_assert(not std::is_default_constructible_v<orm::Database>);
static_assert(std::is_constructible_v<orm::Database, const orm::SqliteConfig&>);
static_assert(not std::is_copy_constructible_v<orm::Database>);
static_assert(not std::is_move_constructible_v<orm::Database>);
static_assert(not std::is_copy_assignable_v<orm::Database>);
static_assert(not std::is_move_assignable_v<orm::Database>);
static_assert(std::is_nothrow_copy_constructible_v<ApplicationContext>);
static_assert(std::is_nothrow_move_constructible_v<ApplicationContext>);
static_assert(std::is_nothrow_copy_assignable_v<ApplicationContext>);
static_assert(std::is_nothrow_move_assignable_v<ApplicationContext>);
static_assert(sizeof(ApplicationContext) == sizeof(orm::Database*));
static_assert(std::same_as<decltype(std::declval<orm::Database&>().orm<ApplicationSchema>()), ApplicationContext>);
static_assert(noexcept(std::declval<orm::Database&>().orm<ApplicationSchema>()));
// External compilation instantiates the schema-bound facade and both SOCI conversion directions.
[[maybe_unused]] auto instantiateSchemaBoundApi(ApplicationContext& database, soci::values& values) -> void
{
    orm::Query<Employee> query;
    (void)database.select(query);

    orm::ProjectionQuery<Employee, EmployeeName> projection;
    projection.project(orm::query::as("name", orm::query::col<&Employee::department, &Department::name>()));
    (void)database.select(projection);

    database.insert(Employee{});
    database.insert(std::vector<Employee>{});

    orm::Update<Employee> update;
    update.set(orm::query::col<&Employee::rank>(), 2).where(orm::query::col<&Employee::id>() == 1);
    (void)database.update(update);
    (void)database.remove<Employee>(orm::query::col<&Employee::id>() == 1);
    database.createTable<Employee>();
    database.deleteTable<Employee>();

    using Payload = ApplicationContext::Payload<Employee>;
    Payload payload{};
    auto indicator = soci::i_ok;
    soci::type_conversion<Payload>::to_base(payload, values, indicator);
    soci::type_conversion<Payload>::from_base(values, indicator, payload);
}
} // namespace
