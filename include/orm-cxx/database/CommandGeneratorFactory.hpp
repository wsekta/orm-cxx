#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>

#include "BackendProvider.hpp"
#include "BackendType.hpp"
#include "CommandGenerator.hpp"

namespace orm::db
{
class CommandGeneratorFactory
{
public:
    CommandGeneratorFactory();
    CommandGeneratorFactory(const CommandGeneratorFactory&) = delete;
    CommandGeneratorFactory(CommandGeneratorFactory&&) = default;
    auto operator=(const CommandGeneratorFactory&) -> CommandGeneratorFactory& = delete;
    auto operator=(CommandGeneratorFactory&&) -> CommandGeneratorFactory& = default;

    auto registerBackend(std::unique_ptr<BackendProvider> backend) -> void;
    [[nodiscard]] auto getBackend(BackendType backendType) const -> const BackendProvider&;
    [[nodiscard]] auto findBackend(std::string_view connectionString) const noexcept -> const BackendProvider*;
    auto getCommandGenerator(BackendType backendType) const -> const CommandGenerator&;

private:
    std::unordered_map<BackendType, std::unique_ptr<BackendProvider>> backends;
};
} // namespace orm::db
