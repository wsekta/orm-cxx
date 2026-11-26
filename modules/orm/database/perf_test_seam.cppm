module;

#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>

export module orm:perf_test_seam;

import :database_context;

/**
 * Internal perf-test seam for the benchmark gate (variant C).
 *
 * Compiled and exported only when ORM_CXX_ENABLE_PERF_TEST_SEAM=ON. It calls the same
 * production insertManyImpl as the public API and differs only in the chunk policy.
 */
namespace orm::detail::perf_seam
{
class TestAccess
{
public:
    template <typename SchemaType, typename T>
    static auto insertManyWithChunkRows(OrmContext<SchemaType>& context, std::span<const T> rows,
                                        std::size_t chunkRows) -> void
    {
        context.template insertManyImpl<T>(rows, std::optional<std::size_t>{chunkRows});
    }
};

export [[nodiscard]] inline auto defaultChunkRowsForTest() noexcept -> std::size_t
{
    return insertManyDefaultRowBudget;
}

export template <typename SchemaType, typename T>
auto insertManyWithChunkRowsForTest(OrmContext<SchemaType>& context, std::span<const T> rows,
                                    std::size_t chunkRows) -> void
{
    if (chunkRows == 0)
    {
        throw std::invalid_argument{"A batch insert chunk cannot be empty"};
    }
    TestAccess::insertManyWithChunkRows(context, rows, chunkRows);
}
} // namespace orm::detail::perf_seam
