#include "ffi/geoscatter3d_ffi.h"

#include <jni.h>

#include <cstdint>
#include <string>

namespace {

gs3d_pipeline* pipeline(jlong handle) {
    return reinterpret_cast<gs3d_pipeline*>(static_cast<intptr_t>(handle));
}

std::string utf8(JNIEnv* env, jbyteArray bytes) {
    if (!bytes) return {};
    const jsize size = env->GetArrayLength(bytes);
    std::string value(static_cast<size_t>(size), '\0');
    if (size) env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(value.data()));
    return value;
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_geoscatter3d_ui_1flutter_PipelineNative_create(JNIEnv*, jobject) {
    gs3d_pipeline_config config{sizeof(gs3d_pipeline_config)};
    return static_cast<jlong>(reinterpret_cast<intptr_t>(gs3d_pipeline_create(&config)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_geoscatter3d_ui_1flutter_PipelineNative_start(
    JNIEnv* env, jobject, jlong handle, jbyteArray source, jbyteArray bundle,
    jint points_per_chunk, jboolean resume) {
    try {
        const std::string source_path = utf8(env, source);
        const std::string bundle_path = utf8(env, bundle);
        if (env->ExceptionCheck() || bundle_path.find('\0') != std::string::npos ||
            source_path.find('\0') != std::string::npos || points_per_chunk <= 0) return -1;
        uint64_t task = 0;
        int32_t result;
        if (resume) {
            result = gs3d_pipeline_resume(pipeline(handle), bundle_path.c_str(), &task);
        } else {
            gs3d_pipeline_job job{sizeof(gs3d_pipeline_job), source_path.c_str(),
                                  bundle_path.c_str(), static_cast<uint32_t>(points_per_chunk), nullptr};
            result = gs3d_pipeline_start(pipeline(handle), &job, &task);
        }
        return result == 0 ? static_cast<jlong>(task) : result;
    } catch (...) {
        return -3;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_geoscatter3d_ui_1flutter_PipelineNative_poll(
    JNIEnv* env, jobject, jlong handle, jlongArray values, jbyteArray stage, jbyteArray error) {
    if (!values || !stage || !error || env->GetArrayLength(values) < 4 ||
        env->GetArrayLength(stage) < 32 || env->GetArrayLength(error) < 256) return -1;
    gs3d_pipeline_status status{};
    status.struct_size = sizeof(status);
    const int32_t result = gs3d_pipeline_poll(pipeline(handle), &status);
    if (result != 0) return result;
    const jlong output[] = {static_cast<jlong>(status.task_id), static_cast<jlong>(status.state),
                            static_cast<jlong>(status.points), static_cast<jlong>(status.chunks)};
    env->SetLongArrayRegion(values, 0, 4, output);
    env->SetByteArrayRegion(stage, 0, 32, reinterpret_cast<const jbyte*>(status.stage));
    env->SetByteArrayRegion(error, 0, 256, reinterpret_cast<const jbyte*>(status.error));
    return env->ExceptionCheck() ? -3 : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_geoscatter3d_ui_1flutter_PipelineNative_cancel(JNIEnv*, jobject, jlong handle, jlong task) {
    return gs3d_pipeline_cancel(pipeline(handle), static_cast<uint64_t>(task));
}
