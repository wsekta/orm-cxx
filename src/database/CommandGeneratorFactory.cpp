#include "orm-cxx/database/CommandGeneratorFactory.hpp"

#include <stdexcept>
#include <utility>

#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
#include "orm-cxx/database/sqlite/SqliteBackend.hpp"
#endif

namespace orm::db
{
CommandGeneratorFactory::CommandGeneratorFactory()
{
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
    registerBackend(std::make_unique<sqlite::SqliteBackend>());
#endif
}

auto CommandGeneratorFactory::registerBackend(std::unique_ptr<BackendProvider> backend) -> void
{
    if (backend == nullptr)
    {
        throw std::invalid_argument{"Cannot register a null backend"};
    }

    const auto backendType = backend->type();

    if (backendType == BackendType::Empty)
    {
        throw std::invalid_argument{"BackendType::Empty is reserved for disconnected databases"};
    }

    if (backends.contains(backendType))
    {
        throw std::invalid_argument{"Backend is already registered"};
    }

    backends.emplace(backendType, std::move(backend));
}

auto CommandGeneratorFactory::getBackend(BackendType backendType) const -> const BackendProvider&
{
    return *backends.at(backendType);
}

auto CommandGeneratorFactory::findBackend(std::string_view connectionString) const noexcept -> const BackendProvider*
{
    const BackendProvider* match = nullptr;

    for (const auto& entry : backends)
    {
        const auto& backend = entry.second;

        if (backend->acceptsConnectionString(connectionString))
        {
            if (match != nullptr)
            {
                // Predicate-based discovery must never depend on the iteration
                // order of the registry. Treat overlapping providers as an
                // ambiguous, unsupported connection string.
                return nullptr;
            }

            match = backend.get();
        }
    }

    return match;
}

auto CommandGeneratorFactory::getCommandGenerator(BackendType backendType) const -> const CommandGenerator&
{
    return getBackend(backendType).commandGenerator();
}
} // namespace orm::db
