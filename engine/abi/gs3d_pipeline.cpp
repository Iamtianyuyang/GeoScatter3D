#include "ffi/geoscatter3d_ffi.h"
#include "pipeline/stages/BundleJob.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>

struct gs3d_pipeline {
    gs3d::pipeline::BundleJob job;
};

namespace {

static_assert(static_cast<uint32_t>(gs3d::pipeline::BundleJobState::idle) == GS3D_PIPELINE_IDLE);
static_assert(static_cast<uint32_t>(gs3d::pipeline::BundleJobState::running) == GS3D_PIPELINE_RUNNING);
static_assert(static_cast<uint32_t>(gs3d::pipeline::BundleJobState::done) == GS3D_PIPELINE_DONE);
static_assert(static_cast<uint32_t>(gs3d::pipeline::BundleJobState::failed) == GS3D_PIPELINE_FAILED);
static_assert(static_cast<uint32_t>(gs3d::pipeline::BundleJobState::cancelled) == GS3D_PIPELINE_CANCELLED);

std::filesystem::path utf8_path(const char* path) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path)));
}

template <std::size_t N>
void copy_text(char (&out)[N], const std::string& text) {
    const auto length = std::min(text.size(), N - 1);
    std::memcpy(out, text.data(), length);
    out[length] = '\0';
}

} // namespace

extern "C" {

gs3d_pipeline* gs3d_pipeline_create(const gs3d_pipeline_config* config) {
    if (!config || config->struct_size < sizeof(gs3d_pipeline_config)) {
        return nullptr;
    }
    try {
        return new gs3d_pipeline;
    } catch (...) {
        return nullptr;
    }
}

int32_t gs3d_pipeline_start(gs3d_pipeline* pipeline, const gs3d_pipeline_job* job, uint64_t* task) {
    if (!pipeline || !job || job->struct_size < sizeof(gs3d_pipeline_job) || !task ||
        !job->source_path || !*job->source_path || !job->bundle_path || !*job->bundle_path ||
        job->points_per_chunk == 0 || job->points_per_chunk > 65536) {
        return -1;
    }
    try {
        gs3d::pipeline::BundleBuildOptions options;
        options.source = utf8_path(job->source_path);
        options.bundle_dir = utf8_path(job->bundle_path);
        options.points_per_chunk = job->points_per_chunk;
        if (job->value_field) {
            options.value_field = job->value_field;
        }
        *task = pipeline->job.start(std::move(options));
        return 0;
    } catch (...) {
        return -3;
    }
}

int32_t gs3d_pipeline_resume(gs3d_pipeline* pipeline, const char* bundle_dir, uint64_t* task) {
    if (!pipeline || !bundle_dir || !*bundle_dir || !task) {
        return -1;
    }
    try {
        *task = pipeline->job.resume(utf8_path(bundle_dir));
        return 0;
    } catch (...) {
        return -3;
    }
}

int32_t gs3d_pipeline_poll(gs3d_pipeline* pipeline, gs3d_pipeline_status* out) {
    if (!pipeline || !out || out->struct_size < sizeof(gs3d_pipeline_status)) {
        return -1;
    }
    try {
        const auto status = pipeline->job.poll();
        std::memset(out, 0, sizeof(*out));
        out->struct_size = sizeof(*out);
        out->task_id = status.id;
        out->state = static_cast<uint32_t>(status.state);
        copy_text(out->stage, status.stage);
        copy_text(out->error, status.error);
        out->points = status.points;
        out->chunks = status.chunks;
        return 0;
    } catch (...) {
        return -3;
    }
}

int32_t gs3d_pipeline_cancel(gs3d_pipeline* pipeline, uint64_t task) {
    if (!pipeline || task == 0) {
        return -1;
    }
    try {
        return pipeline->job.cancel(task) ? 0 : -2;
    } catch (...) {
        return -3;
    }
}

void gs3d_pipeline_destroy(gs3d_pipeline* pipeline) {
    delete pipeline;
}

} // extern "C"
