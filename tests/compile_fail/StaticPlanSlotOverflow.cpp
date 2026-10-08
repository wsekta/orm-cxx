#include <limits>

#include "StaticPlanModels.hpp"

int main()
{
    using namespace orm::query;
    using namespace static_plan_models;
    const auto plan = select<User>().where(col<&User::id>() == param<int, std::numeric_limits<std::size_t>::max()>());
    (void)plan.toDynamic();
}
