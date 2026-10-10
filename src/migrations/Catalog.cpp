module;

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module orm;

import :migrations_internal;

namespace orm::migrations
{
MigrationError::MigrationError(ErrorCode code, std::string message, Version version, db::BackendType backend,
                               std::optional<std::string> nativeCode)
    : std::runtime_error{std::move(message)},
      code_{code},
      version_{version},
      backend_{backend},
      nativeCode_{std::move(nativeCode)}
{
}
auto MigrationError::getCode() const noexcept -> ErrorCode
{
    return code_;
}
auto MigrationError::getVersion() const noexcept -> Version
{
    return version_;
}
auto MigrationError::getBackendType() const noexcept -> db::BackendType
{
    return backend_;
}
auto MigrationError::getNativeCode() const noexcept -> const std::optional<std::string>&
{
    return nativeCode_;
}

namespace detail
{
auto backendName(db::BackendType backend) -> std::string_view
{
    if (backend == db::BackendType::Sqlite)
    {
        return "sqlite";
    }
    if (backend == db::BackendType::Postgres)
    {
        return "postgresql";
    }
    throw MigrationError{ErrorCode::UnsupportedBackend, "Migrations require SQLite or PostgreSQL", 0, backend};
}

auto normalizeSql(std::string_view sql) -> std::string
{
    if (sql.starts_with("\xef\xbb\xbf"))
    {
        sql.remove_prefix(3);
    }
    if (sql.find('\0') != std::string_view::npos)
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Migration SQL contains a NUL byte"};
    }
    std::string result;
    result.reserve(sql.size());
    for (std::size_t i = 0; i < sql.size(); ++i)
    {
        if (sql[i] == '\r' && i + 1 < sql.size() && sql[i + 1] == '\n')
        {
            continue;
        }
        result.push_back(sql[i]);
    }
    return result;
}

// FIPS 180-4 SHA-256; portable bytes and fixed-width arithmetic, no runtime dependency.
auto sha256(std::string_view bytes) -> std::string
{
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<unsigned char> data(bytes.begin(), bytes.end());
    const auto bitLength = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80);
    while (data.size() % 64 != 56)
    {
        data.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        data.push_back(static_cast<unsigned char>(bitLength >> shift));
    }
    for (std::size_t block = 0; block < data.size(); block += 64)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i)
        {
            for (std::size_t j = 0; j < 4; ++j)
            {
                words[i] = (words[i] << 8U) | data[block + i * 4 + j];
            }
        }
        for (std::size_t i = 16; i < words.size(); ++i)
        {
            const auto a = words[i - 15];
            const auto b = words[i - 2];
            words[i] = words[i - 16] + (std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3U)) + words[i - 7] +
                       (std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10U));
        }
        auto work = state;
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            const auto first = work[7] + (std::rotr(work[4], 6) ^ std::rotr(work[4], 11) ^ std::rotr(work[4], 25)) +
                               ((work[4] & work[5]) ^ (~work[4] & work[6])) + constants[i] + words[i];
            const auto second = (std::rotr(work[0], 2) ^ std::rotr(work[0], 13) ^ std::rotr(work[0], 22)) +
                                ((work[0] & work[1]) ^ (work[0] & work[2]) ^ (work[1] & work[2]));
            for (std::size_t j = 7; j > 0; --j)
            {
                work[j] = work[j - 1];
            }
            work[4] += first;
            work[0] = first + second;
        }
        for (std::size_t i = 0; i < state.size(); ++i)
        {
            state[i] += work[i];
        }
    }
    constexpr std::string_view hex = "0123456789abcdef";
    std::string result;
    for (const auto word : state)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            result.push_back(hex[(word >> shift) & 15U]);
        }
    }
    return result;
}

auto checksum(const SqlMigration& scripts) -> std::string
{
    // Length-prefixed fields distinguish boundaries and absent versus empty down scripts.
    std::string data = "orm-cxx-sql-migration-v1:" + std::to_string(scripts.up.size()) + ":" + scripts.up;
    data += scripts.down ? ":1:" + std::to_string(scripts.down->size()) + ":" + *scripts.down : ":0:";
    return sha256(data);
}
} // namespace detail

namespace
{
auto safeName(std::string_view name) -> bool
{
    return !name.empty() && std::ranges::all_of(name,
                                                [](char c) {
                                                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                           (c >= '0' && c <= '9') || c == '_' || c == '-';
                                                });
}
auto readSql(const std::filesystem::path& path) -> std::string
{
    if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path))
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Migration SQL must be a regular file"};
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Cannot read migration SQL"};
    }
    std::string sql{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    if (stream.bad())
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Cannot read migration SQL"};
    }
    return sql;
}
} // namespace

Catalog::Catalog(std::initializer_list<Migration> migrations) : Catalog{std::vector<Migration>{migrations}} {}
Catalog::Catalog(std::vector<Migration> migrations) : migrations_{std::move(migrations)}
{
    std::ranges::sort(migrations_, {}, &Migration::version);
    Version previous = 0;
    for (auto& migration : migrations_)
    {
        if (migration.version <= previous || !safeName(migration.name) || migration.scripts.empty())
        {
            throw MigrationError{ErrorCode::InvalidCatalog,
                                 "Migration versions must be positive and unique, with a name and SQL",
                                 migration.version};
        }
        previous = migration.version;
        for (auto& [backend, scripts] : migration.scripts)
        {
            (void)detail::backendName(backend);
            scripts.up = detail::normalizeSql(scripts.up);
            if (scripts.up.empty())
            {
                throw MigrationError{ErrorCode::InvalidCatalog, "Migration up SQL is empty", migration.version,
                                     backend};
            }
            if (scripts.down)
            {
                scripts.down = detail::normalizeSql(*scripts.down);
            }
        }
    }
}
auto Catalog::migrations() const noexcept -> std::span<const Migration>
{
    return migrations_;
}

auto Catalog::fromDirectory(const std::filesystem::path& directory) -> Catalog
{
    try
    {
        if (std::filesystem::is_symlink(directory))
        {
            throw MigrationError{ErrorCode::InvalidCatalog, "Migration directory cannot be a symlink"};
        }
        std::vector<Migration> migrations;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            const auto filename = entry.path().filename().string();
            const auto separator = filename.find('_');
            Version version = 0;
            if (separator == std::string::npos || !entry.is_directory() || entry.is_symlink())
            {
                throw MigrationError{ErrorCode::InvalidCatalog, "Expected a <version>_<name> migration directory"};
            }
            const auto parsed = std::from_chars(filename.data(), filename.data() + separator, version);
            if (parsed.ec != std::errc{} || parsed.ptr != filename.data() + separator)
            {
                throw MigrationError{ErrorCode::InvalidCatalog, "Invalid migration directory version"};
            }
            Migration migration{.version = version, .name = filename.substr(separator + 1), .scripts = {}};
            for (const auto& dialect : std::filesystem::directory_iterator(entry.path()))
            {
                const auto name = dialect.path().filename().string();
                if (!dialect.is_directory() || dialect.is_symlink() || (name != "sqlite" && name != "postgresql"))
                {
                    throw MigrationError{ErrorCode::InvalidCatalog, "Expected a sqlite or postgresql directory",
                                         version};
                }
                for (const auto& file : std::filesystem::directory_iterator(dialect.path()))
                {
                    if (!file.is_regular_file() || file.is_symlink())
                    {
                        throw MigrationError{ErrorCode::InvalidCatalog, "Migration SQL must be a regular file",
                                             version};
                    }
                    if (file.path().filename() != "up.sql" && file.path().filename() != "down.sql")
                    {
                        throw MigrationError{ErrorCode::InvalidCatalog, "Unexpected file in migration directory",
                                             version};
                    }
                }
                SqlMigration scripts{.up = readSql(dialect.path() / "up.sql"), .down = std::nullopt};
                const auto down = dialect.path() / "down.sql";
                if (std::filesystem::exists(down))
                {
                    scripts.down = readSql(down);
                }
                migration.scripts.emplace(name == "sqlite" ? db::BackendType::Sqlite : db::BackendType::Postgres,
                                          std::move(scripts));
            }
            migrations.push_back(std::move(migration));
        }
        return Catalog{std::move(migrations)};
    }
    catch (const std::filesystem::filesystem_error&)
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Cannot access migration directory"};
    }
}
} // namespace orm::migrations
