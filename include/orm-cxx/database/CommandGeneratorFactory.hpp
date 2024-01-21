#pragma once

#include "BackendType.hpp"
#include "CommandGenerator.hpp"

namespace orm::db
{
class CommandGeneratorFactory
{
public:
    CommandGeneratorFactory();

    auto getCommandGenerator(BackendType backendType) const -> const CommandGenerator&;

private:
    std::unordered_map<BackendType, CommandGenerator> commandGenerators;
};
} // namespace orm::db
