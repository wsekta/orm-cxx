#pragma once

#include "BackendType.hpp"
#include "CommandGenerator.hpp"

namespace orm::db
{
class CommandGeneratorFactory
{
public:
    CommandGeneratorFactory();

    auto getCommandGenerator(BackendType backendType) const -> const std::unique_ptr<CommandGenerator>&;

private:
    std::unordered_map<BackendType, std::unique_ptr<CommandGenerator>> commandGenerators;
};
} // namespace orm::db
