#include "util/Log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

namespace {

class SettingsRestore {
public:
    SettingsRestore()
        : previous_(gs3d::util::log::settings())
    {
    }

    ~SettingsRestore()
    {
        gs3d::util::log::configure(previous_);
    }

private:
    gs3d::util::log::Settings previous_;
};

} // namespace

TEST_CASE("Log levels parse case-insensitively", "[log]")
{
    CHECK(gs3d::util::log::parse_level("DEBUG") ==
          gs3d::util::log::Level::Debug);
    CHECK(gs3d::util::log::parse_level("warn") ==
          gs3d::util::log::Level::Warning);
    CHECK(gs3d::util::log::parse_level("quiet") ==
          gs3d::util::log::Level::Off);
    CHECK_FALSE(gs3d::util::log::parse_level("verbose").has_value());
}

TEST_CASE("Log settings filter levels and benchmark channel", "[log]")
{
    SettingsRestore restore;
    gs3d::util::log::configure({
        .minimum_level = gs3d::util::log::Level::Warning,
        .benchmark_enabled = false
    });

    CHECK_FALSE(gs3d::util::log::should_log(
        gs3d::util::log::Level::Info,
        gs3d::util::log::Channel::General
    ));
    CHECK(gs3d::util::log::should_log(
        gs3d::util::log::Level::Error,
        gs3d::util::log::Channel::General
    ));
    CHECK_FALSE(gs3d::util::log::should_log(
        gs3d::util::log::Level::Warning,
        gs3d::util::log::Channel::Benchmark
    ));

}

TEST_CASE("Log lines preserve the level and benchmark channels", "[log]")
{
    SettingsRestore restore;
    std::ostringstream captured;
    auto* const previous_stdout = std::cout.rdbuf(captured.rdbuf());

    gs3d::util::log::configure({
        .minimum_level = gs3d::util::log::Level::Info,
        .benchmark_enabled = true
    });
    {
        auto line = gs3d::util::log::info();
        line << "viewer initialized";
    }
    {
        auto line = gs3d::util::log::benchmark();
        line << "frame_count = 60";
    }

    gs3d::util::log::configure({
        .minimum_level = gs3d::util::log::Level::Info,
        .benchmark_enabled = false
    });
    {
        auto line = gs3d::util::log::benchmark();
        line << "must not be emitted";
    }
    std::cout.rdbuf(previous_stdout);

    CHECK(captured.str() ==
          "[info] viewer initialized\n"
          "[info][benchmark] frame_count = 60\n");
}
