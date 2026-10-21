module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::test::fixtures;

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
static_assert(std::is_constructible_v<orm::Database, orm::SqliteConfig>);
static_assert(not std::is_constructible_v<orm::Database, std::string>);
static_assert(not std::is_constructible_v<orm::Database, orm::db::CommandGeneratorFactory>);
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
static_assert(
    std::same_as<ApplicationContext::Payload<Employee>, orm::db::binding::BindingPayload<Employee, ApplicationSchema>>);

} // namespace
