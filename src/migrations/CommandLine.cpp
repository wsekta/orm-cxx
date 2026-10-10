module;

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

module orm;

import :migrations_internal;

namespace orm::migrations
{
auto detail::runCommandLineWithConnection(std::span<const std::string_view> args, const Catalog& catalog,
                                          std::optional<std::string_view> connection, std::ostream& output,
                                          std::ostream& errors) -> int
{
    constexpr std::string_view usage = "Usage: status | validate | preview up|down [--to N] | up [--to N] | down --to "
                                       "N | baseline --to N\nConnection: ORM_CXX_DATABASE_URL\n";
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
        command != "baseline")
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
    if (args.size() != consumed)
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
        if (!connection || connection->empty())
        {
            errors << "Set ORM_CXX_DATABASE_URL before running migrations\n";
            return 1;
        }
        Database database;
        database.connect(std::string{*connection});
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
            for (const auto& issue : status.problems)
            {
                errors << "Migration " << issue.version << ": " << issue.message << '\n';
            }
            return status.isValid() ? 0 : 1;
        }
        if (command == "preview")
        {
            const auto plan = runner.preview(direction, target);
            output << "From " << plan.from << " to " << plan.to << '\n';
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
auto runCommandLine(std::span<const std::string_view> args, const Catalog& catalog, std::ostream& output,
                    std::ostream& errors) -> int
{
    const auto* connection = std::getenv("ORM_CXX_DATABASE_URL");
    return detail::runCommandLineWithConnection(
        args, catalog, connection == nullptr ? std::nullopt : std::optional<std::string_view>{connection}, output,
        errors);
}
auto runCommandLine(int argc, const char* const* argv, const Catalog& catalog) -> int
{
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }
    return runCommandLine(args, catalog, std::cout, std::cerr);
}
} // namespace orm::migrations
