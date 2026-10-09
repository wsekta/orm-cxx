#include <array>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>

#ifdef _MSC_VER
#include <malloc.h>
#endif

import orm;

extern "C" auto orm_cxx_build_copy_plans(int age) -> std::size_t;

namespace
{
std::atomic<bool> countAllocations{false};
std::atomic<std::size_t> allocationCount{0};
std::atomic<std::size_t> consumedState{0};

auto recordAllocation() noexcept -> void
{
    if (countAllocations.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

auto operator new(std::size_t size) -> void*
{
    recordAllocation();
    if (auto* storage = std::malloc(size == 0 ? 1 : size))
        return storage;
    throw std::bad_alloc{};
}

auto operator new[](std::size_t size) -> void*
{
    return ::operator new(size);
}

auto operator delete(void* storage) noexcept -> void
{
    std::free(storage);
}

auto operator delete[](void* storage) noexcept -> void
{
    ::operator delete(storage);
}

auto operator delete(void* storage, std::size_t) noexcept -> void
{
    ::operator delete(storage);
}

auto operator delete[](void* storage, std::size_t) noexcept -> void
{
    ::operator delete[](storage);
}

auto operator new(std::size_t size, std::align_val_t requestedAlignment) -> void*
{
    recordAllocation();
    const auto alignment = static_cast<std::size_t>(requestedAlignment);
#ifdef _MSC_VER
    auto* storage = _aligned_malloc(size == 0 ? alignment : size, alignment);
#else
    if (size > std::numeric_limits<std::size_t>::max() - (alignment - 1))
        throw std::bad_alloc{};
    const auto roundedSize = size == 0 ? alignment : ((size + alignment - 1) / alignment) * alignment;
    auto* storage = std::aligned_alloc(alignment, roundedSize);
#endif
    if (storage != nullptr)
        return storage;
    throw std::bad_alloc{};
}

auto operator new[](std::size_t size, std::align_val_t alignment) -> void*
{
    return ::operator new(size, alignment);
}

auto operator delete(void* storage, std::align_val_t) noexcept -> void
{
#ifdef _MSC_VER
    _aligned_free(storage);
#else
    std::free(storage);
#endif
}

auto operator delete[](void* storage, std::align_val_t alignment) noexcept -> void
{
    ::operator delete(storage, alignment);
}

auto operator delete(void* storage, std::size_t, std::align_val_t alignment) noexcept -> void
{
    ::operator delete(storage, alignment);
}

auto operator delete[](void* storage, std::size_t, std::align_val_t alignment) noexcept -> void
{
    ::operator delete[](storage, alignment);
}

int main(int argc, char**)
{
    try
    {
        countAllocations.store(true);
        auto* probe = ::operator new(64);
        countAllocations.store(false);
        ::operator delete(probe);
        if (allocationCount.load() != 1)
            return 2;

        allocationCount.store(0);
        countAllocations.store(true);
        for (int i = 0; i < 1000; ++i)
            consumedState.fetch_add(orm_cxx_build_copy_plans(argc + i), std::memory_order_relaxed);
        countAllocations.store(false);
        const auto allocations = allocationCount.load();
        std::cout << "Built and copied 4000 plans; allocations: " << allocations
                  << "; checksum: " << consumedState.load() << '\n';
        return allocations == 0 && consumedState.load() != 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        countAllocations.store(false);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
