#pragma once

#include <memory>
#include <unordered_map>

#include "orm-cxx/database/BackendType.hpp"
#include "orm-cxx/database/TypeTranslator.hpp"

namespace orm::db
{
class TypeTranslatorFactory
{
public:
    TypeTranslatorFactory();

    auto getTranslator(BackendType backendType) const -> const std::unique_ptr<TypeTranslator>&;

private:
    std::unordered_map<BackendType, std::unique_ptr<TypeTranslator>> translators;
};
}