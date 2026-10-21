module;

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

module orm;

import :migrations_internal;

namespace orm::migrations
{
auto detail::runCatalogCommandWithConnection(std::span<const std::string_view> args, const Catalog& catalog,
                                             const ConnectionEnvironment& connection, std::ostream& output,
                                             std::ostream& errors) -> int
{
    constexpr std::string_view usage =
        "Usage: status | validate | preview up|down [--to N] | up [--to N] | down --to "
        "N | baseline --to N | resolve --version N --checksum SHA256 "
        "--resolution completed|reverted\nConnection: ORM_CXX_DATABASE_BACKEND and ORM_CXX_DATABASE_* fields\n";
    if (args.empty() || (args.size() == 1 && (args[0] == "--help" || args[0] == "help")))
    {
        output << usage;
        return 0;
    }
    const auto badArguments = [&]
    {
        errors << usage;
        return 2;
    };
    const auto command = args[0];
    if (command != "status" && command != "validate" && command != "preview" && command != "up" && command != "down" &&
        command != "baseline" && command != "resolve")
    {
        return badArguments();
    }
    Direction direction = command == "down" ? Direction::Down : Direction::Up;
    std::size_t consumed = 1;
    if (command == "preview")
    {
        if (args.size() < 2 || (args[1] != "up" && args[1] != "down"))
        {
            return badArguments();
        }
        direction = args[1] == "up" ? Direction::Up : Direction::Down;
        consumed = 2;
    }
    std::optional<Version> target;
    std::string_view expectedChecksum;
    Resolution resolution{Resolution::Completed};
    if (command == "resolve")
    {
        if (args.size() != 7 || args[1] != "--version" || args[3] != "--checksum" || args[5] != "--resolution" ||
            (args[6] != "completed" && args[6] != "reverted"))
        {
            return badArguments();
        }
        Version value{};
        const auto parsed = std::from_chars(args[2].data(), args[2].data() + args[2].size(), value);
        expectedChecksum = args[4];
        if (parsed.ec != std::errc{} || parsed.ptr != args[2].data() + args[2].size() || value <= 0 ||
            expectedChecksum.size() != 64 ||
            expectedChecksum.find_first_not_of("0123456789abcdef") != std::string_view::npos)
        {
            return badArguments();
        }
        target = value;
        resolution = args[6] == "completed" ? Resolution::Completed : Resolution::Reverted;
    }
    else if (args.size() != consumed)
    {
        if (command == "status" || command == "validate" || args.size() != consumed + 2 || args[consumed] != "--to")
        {
            return badArguments();
        }
        Version value{};
        const auto text = args[consumed + 1];
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 0)
        {
            return badArguments();
        }
        target = value;
    }
    if ((command == "down" || command == "baseline") && !target)
    {
        return badArguments();
    }
    try
    {
        const auto config = connectionConfiguration(connection);
        auto database = std::visit([](const auto& value) { return Database{value}; }, config);
        Runner runner{database, catalog};
        if (command == "status")
        {
            const auto status = runner.status();
            output << "Current: " << status.currentVersion << "; baseline: " << status.baselineVersion << '\n';
            for (const auto& migration : status.applied)
            {
                output << migration.version << ' ' << migration.name
                       << (migration.baseline ? " baseline\n" : " applied\n");
            }
            for (const auto& migration : status.pending)
            {
                output << migration.version << ' ' << migration.name << " pending\n";
            }
            if (status.incomplete)
            {
                const auto& marker = *status.incomplete;
                output << "Incomplete: " << marker.version << ' ' << marker.name << ' '
                       << (marker.direction == Direction::Up ? "up" : "down") << " checksum " << marker.checksum
                       << '\n';
                errors << "Inspect and repair the database before resolving the incomplete migration\n";
            }
            for (const auto& issue : status.problems)
            {
                errors << "Migration " << issue.version << ": " << issue.message << '\n';
            }
            return status.isValid() ? 0 : 1;
        }
        if (command == "preview")
        {
            const auto plan = runner.preview(direction, target);
            output << "From " << plan.from << " to " << plan.to << "; atomicity: "
                   << (plan.atomicity == db::MigrationAtomicity::WholeMigration ? "whole migration" :
                                                                                  "nontransactional")
                   << '\n';
            for (const auto& migration : plan.migrations)
            {
                output << "-- " << migration.version << ' ' << migration.name << '\n' << migration.sql << '\n';
            }
            return 0;
        }
        if (command == "validate")
        {
            runner.validate();
        }
        else if (command == "up")
        {
            runner.up(target);
        }
        else if (command == "down")
        {
            runner.down(*target);
        }
        else if (command == "resolve")
        {
            runner.resolve(*target, expectedChecksum, resolution);
        }
        else
        {
            runner.baseline(*target);
        }
        output << "Migration command completed\n";
        return 0;
    }
    catch (const MigrationError& error)
    {
        errors << "Migration " << error.getVersion() << ": " << error.what();
        if (error.getNativeCode())
        {
            errors << " (" << *error.getNativeCode() << ')';
        }
        errors << '\n';
        return 1;
    }
    catch (const std::exception&)
    {
        errors << "Cannot connect to or operate on the migration database\n";
        return 1;
    }
}
auto runCommandLine(std::span<const std::string_view> args, model::SchemaView models,
                    const std::filesystem::path& directory, std::ostream& output, std::ostream& errors) -> int
{
    constexpr std::string_view usage =
        "Usage: init [--backends sqlite,postgresql,mysql,mariadb] | compare [--check] [--decisions FILE] | "
        "make NAME [--decisions FILE | --manual] | finalize VERSION --accept-schema | status | validate | "
        "preview up|down [--to N] | up [--to N] | down --to N | baseline --to N | "
        "resolve --version N --checksum SHA256 --resolution completed|reverted\n"
        "Generation is offline. Execution uses ORM_CXX_DATABASE_BACKEND and ORM_CXX_DATABASE_* fields.\n";
    if (args.empty() || (args.size() == 1 && (args[0] == "--help" || args[0] == "help")))
    {
        output << usage;
        return 0;
    }
    const auto badArguments = [&]
    {
        errors << usage;
        return 2;
    };
    const auto command = args.front();
    if (command != "init" && command != "compare" && command != "make" && command != "finalize" &&
        command != "status" && command != "validate" && command != "preview" && command != "up" && command != "down" &&
        command != "baseline" && command != "resolve")
        return badArguments();
    try
    {
        if (command == "init")
        {
            std::vector<db::BackendType> backends;
            if (args.size() == 1)
                backends.assign(generationBackends.begin(), generationBackends.end());
            else if (args.size() == 3 && args[1] == "--backends")
            {
                auto names = args[2];
                while (!names.empty())
                {
                    const auto comma = names.find(',');
                    const auto name = names.substr(0, comma);
                    std::optional<db::BackendType> selected;
                    for (const auto backend : generationBackends)
                    {
                        if (detail::backendName(backend) == name)
                            selected = backend;
                    }
                    if (!selected)
                        return badArguments();
                    backends.push_back(*selected);
                    if (comma == std::string_view::npos)
                        break;
                    names.remove_prefix(comma + 1);
                    if (names.empty())
                        return badArguments();
                }
                if (backends.empty())
                    return badArguments();
            }
            else
                return badArguments();
            (void)MigrationProject::init(directory, backends);
            output << "Migration project initialized\n";
            return 0;
        }
        if (command == "compare" || command == "make")
        {
            bool check{};
            bool manual{};
            std::optional<std::filesystem::path> decisionsFile;
            std::string name;
            std::size_t index = 1;
            if (command == "make")
            {
                if (args.size() < 2 || args[1].starts_with("--"))
                    return badArguments();
                name = args[1];
                index = 2;
            }
            while (index < args.size())
            {
                if (args[index] == "--check" && command == "compare" && !check)
                {
                    check = true;
                    ++index;
                }
                else if (args[index] == "--manual" && command == "make" && !manual)
                {
                    manual = true;
                    ++index;
                }
                else if (args[index] == "--decisions" && !decisionsFile && index + 1 < args.size())
                {
                    decisionsFile = std::filesystem::path{args[index + 1]};
                    index += 2;
                }
                else
                    return badArguments();
            }
            if (manual && decisionsFile)
                return badArguments();
            GenerationDecisions decisions;
            if (decisionsFile)
            {
                std::ifstream input{*decisionsFile, std::ios::binary};
                if (!input)
                    throw MigrationError{ErrorCode::InvalidCatalog, "Cannot read generation decisions file"};
                const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
                decisions = detail::decisionsFromJson(text);
            }
            MigrationProject project{directory};
            if (command == "make")
            {
                const auto version = manual ? project.makeManual(std::move(name), models) :
                                              project.make(std::move(name), models, decisions);
                output << "Draft migration " << version << " created; review SQL and finalize with --accept-schema\n";
                return 0;
            }
            const auto diff = project.compare(models, decisions);
            for (const auto& change : diff.changes)
            {
                output << detail::backendName(change.backend) << ' ' << change.id << ": " << change.description
                       << (change.unresolved ? " [decision required]" : "") << '\n';
            }
            bool invalid{};
            for (const auto& diagnostic : diff.diagnostics)
            {
                errors << diagnostic.code << ": " << diagnostic.message << '\n';
                invalid = invalid || diagnostic.error;
            }
            if (diff.changes.empty())
                output << "Models match the latest ready snapshot\n";
            return invalid || (check && !diff.changes.empty()) ? 1 : 0;
        }
        if (command == "finalize")
        {
            if (args.size() != 3 || args[2] != "--accept-schema")
                return badArguments();
            Version version{};
            const auto parsed = std::from_chars(args[1].data(), args[1].data() + args[1].size(), version);
            if (parsed.ec != std::errc{} || parsed.ptr != args[1].data() + args[1].size() || version <= 0)
            {
                return badArguments();
            }
            MigrationProject{directory}.finalize(version, models, true);
            output << "Migration " << version << " finalized and immutable\n";
            return 0;
        }
        return detail::runCatalogCommandWithConnection(args, Catalog::fromDirectory(directory),
                                                       detail::connectionEnvironmentFromProcess(), output, errors);
    }
    catch (const GenerationError& error)
    {
        errors << error.what() << '\n';
        for (const auto& diagnostic : error.diagnostics())
            errors << diagnostic.code << ": " << diagnostic.message << '\n';
        return 1;
    }
    catch (const MigrationError& error)
    {
        errors << "Migration " << error.getVersion() << ": " << error.what() << '\n';
        return 1;
    }
    catch (const std::exception&)
    {
        errors << "Cannot operate on the migration project\n";
        return 1;
    }
}

auto runCommandLine(int argc, const char* const* argv, model::SchemaView models,
                    const std::filesystem::path& directory) -> int
{
    std::vector<std::string_view> args;
    for (int index = 1; index < argc; ++index)
        args.emplace_back(argv[index]);
    return runCommandLine(args, models, directory, std::cout, std::cerr);
}
} // namespace orm::migrations
