#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <syncstream>

namespace gs3d::util::log {

enum class Level {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Off
};

enum class Channel {
    General,
    Benchmark
};

struct Settings {
    Level minimum_level = Level::Info;
    bool benchmark_enabled = true;
};

[[nodiscard]]
inline std::optional<Level> parse_level(std::string_view value)
{
    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );

    if (normalized == "trace") {
        return Level::Trace;
    }
    if (normalized == "debug") {
        return Level::Debug;
    }
    if (normalized == "info") {
        return Level::Info;
    }
    if (normalized == "warn" || normalized == "warning") {
        return Level::Warning;
    }
    if (normalized == "error" || normalized == "err") {
        return Level::Error;
    }
    if (normalized == "off" || normalized == "quiet") {
        return Level::Off;
    }
    return std::nullopt;
}

[[nodiscard]]
inline Settings settings_from_environment()
{
    Settings result;
    if (const auto* level = std::getenv("GS3D_LOG_LEVEL")) {
        if (const auto parsed = parse_level(level)) {
            result.minimum_level = *parsed;
        }
    }
    if (const auto* benchmark = std::getenv("GS3D_LOG_BENCHMARK")) {
        result.benchmark_enabled =
            std::string_view(benchmark) != "0" &&
            std::string_view(benchmark) != "false";
    }
    return result;
}

inline Settings& mutable_settings()
{
    static Settings settings = settings_from_environment();
    return settings;
}

inline std::mutex& settings_mutex()
{
    static std::mutex mutex;
    return mutex;
}

inline Settings settings()
{
    const std::scoped_lock lock(settings_mutex());
    return mutable_settings();
}

inline void configure(const Settings new_settings)
{
    const std::scoped_lock lock(settings_mutex());
    mutable_settings() = new_settings;
}

[[nodiscard]]
inline bool should_log(const Level level, const Channel channel)
{
    const auto current_settings = settings();
    return level != Level::Off &&
           level >= current_settings.minimum_level &&
           (channel != Channel::Benchmark ||
            current_settings.benchmark_enabled);
}

[[nodiscard]]
inline const char* level_name(const Level level) noexcept
{
    switch (level) {
    case Level::Trace: return "trace";
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warning: return "warning";
    case Level::Error: return "error";
    case Level::Off: return "off";
    }
    return "unknown";
}

class Line {
public:
    Line(const Level level, const Channel channel = Channel::General)
        : level_(level)
        , channel_(channel)
        , enabled_(should_log(level, channel))
    {
    }

    Line(const Line&) = delete;
    Line& operator=(const Line&) = delete;
    Line(Line&&) = delete;
    Line& operator=(Line&&) = delete;

    ~Line() noexcept
    {
        if (!enabled_) {
            return;
        }

        try {
            std::ostream& destination = level_ >= Level::Warning
                ? std::cerr
                : std::cout;
            std::osyncstream output(destination);
            output << '[' << level_name(level_) << ']';
            if (channel_ == Channel::Benchmark) {
                output << "[benchmark]";
            }
            output << ' ' << stream_.str();
            if (stream_.str().empty() || stream_.str().back() != '\n') {
                output << '\n';
            }
        } catch (...) {
            // Logging must never turn an error-reporting path into terminate().
        }
    }

    template <typename T>
    Line& operator<<(const T& value)
    {
        if (enabled_) {
            stream_ << value;
        }
        return *this;
    }

    using StreamManipulator = std::ostream& (*)(std::ostream&);

    Line& operator<<(const StreamManipulator manipulator)
    {
        if (enabled_) {
            manipulator(stream_);
        }
        return *this;
    }

private:
    Level level_;
    Channel channel_;
    bool enabled_ = false;
    std::ostringstream stream_;
};

[[nodiscard]] inline Line trace() { return Line(Level::Trace); }
[[nodiscard]] inline Line debug() { return Line(Level::Debug); }
[[nodiscard]] inline Line info() { return Line(Level::Info); }
[[nodiscard]] inline Line warning() { return Line(Level::Warning); }
[[nodiscard]] inline Line error() { return Line(Level::Error); }
[[nodiscard]] inline Line benchmark()
{
    return Line(Level::Info, Channel::Benchmark);
}

} // namespace gs3d::util::log
