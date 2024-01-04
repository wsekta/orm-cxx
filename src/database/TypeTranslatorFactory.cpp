#include "orm-cxx/database/TypeTranslatorFactory.hpp"

#include "sqlite/SqliteTypeTranslator.hpp"

namespace orm::db
{
TypeTranslatorFactory::TypeTranslatorFactory() : translators()
{
    translators.emplace(BackendType::Sqlite, std::make_unique<sqlite::SqliteTypeTranslator>());
}

auto TypeTranslatorFactory::getTranslator(BackendType backendType) const -> const std::unique_ptr<TypeTranslator>&
{
    return translators.at(backendType);
}
}