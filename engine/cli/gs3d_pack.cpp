#include "foundation/diagnostics/Log.hpp"
#include "pipeline/stages/BundleBuild.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

void usage() {
    std::printf("usage: gs3d_pack --csv <file> --out <bundle-dir> [--chunk-points N]\n"
                "                [--value-field NAME]\n"
                "Builds a v3 bundle (manifest.json + points/*.chunk). Stages already recorded\n"
                "as done for the same source and options are skipped, so an interrupted run\n"
                "resumes from the last finished stage.\n");
}

} // namespace

int main(int argc, char** argv) {
    gs3d::pipeline::BundleBuildOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--csv" && has_value) {
            options.source = argv[++i];
        } else if (arg == "--out" && has_value) {
            options.bundle_dir = argv[++i];
        } else if (arg == "--chunk-points" && has_value) {
            options.points_per_chunk = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--value-field" && has_value) {
            options.value_field = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            gs3d::util::log::error() << "gs3d_pack: unknown argument " << arg;
            usage();
            return 2;
        }
    }
    if (options.source.empty() || options.bundle_dir.empty()) {
        usage();
        return 2;
    }

    try {
        const auto started = std::chrono::steady_clock::now();
        const gs3d::pipeline::BundleBuildResult result =
            gs3d::pipeline::build_bundle(options);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        std::string ran;
        for (const std::string& stage : result.stages_run) {
            ran += (ran.empty() ? "" : ",") + stage;
        }
        std::string skipped;
        for (const std::string& stage : result.stages_skipped) {
            skipped += (skipped.empty() ? "" : ",") + stage;
        }
        std::printf("packed %llu points into %llu chunks in %.2f s (ran: %s; skipped: %s)\n",
                    static_cast<unsigned long long>(result.points),
                    static_cast<unsigned long long>(result.chunks), seconds,
                    ran.empty() ? "-" : ran.c_str(), skipped.empty() ? "-" : skipped.c_str());
        return 0;
    } catch (const std::exception& error) {
        gs3d::util::log::error() << "gs3d_pack: " << error.what();
        return 1;
    }
}
