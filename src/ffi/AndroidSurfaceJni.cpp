#include "ffi/geoscatter3d_ffi.h"

#if defined(__ANDROID__)

#include <android/native_window_jni.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <cstdint>

/*
 * Android 平台 surface 适配层（JNI）。
 *
 * 职责边界（对应 vulkan-core.md §3.1/§7）：
 *   - 平台侧只做「ANativeWindow → surface 句柄」这一件事，
 *     provider 形状与 C ABI 完全一致，因此核心与 ABI 都不出现 Vulkan 类型；
 *   - 引擎侧只拿到 uint64 句柄，不认识 ANativeWindow / Surface / Flutter。
 *
 * 调用顺序：Flutter 侧注册 TextureRegistry.SurfaceProducer（或测试用
 * ImageReader）拿到 Surface → JNI 传进来 → 附加给引擎 → 引擎渲染并 present。
 */
namespace {

const char* const kAndroidSurfaceExtensions[] = {"VK_KHR_android_surface"};

std::int32_t create_android_surface(
    void* instance,
    void* user_data,
    std::uint64_t* out_surface_handle
) {
    auto* window = static_cast<ANativeWindow*>(user_data);
    if (window == nullptr || out_surface_handle == nullptr) {
        return -1;
    }

    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = window;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = vkCreateAndroidSurfaceKHR(
        reinterpret_cast<VkInstance>(instance), &info, nullptr, &surface);
    if (result != VK_SUCCESS) {
        return -3;
    }
    *out_surface_handle = reinterpret_cast<std::uint64_t>(surface);
    return 0;
}

}  // namespace

extern "C" {

/*
 * 附加一个平台 Surface 到引擎。
 *
 * 注意 ANativeWindow 的引用计数：vkCreateAndroidSurfaceKHR 会自持一个引用，
 * 因此本函数在创建 surface 之后必须释放自己这一份，否则窗口永不回收。
 * 回调返回后引擎只依赖 VkSurfaceKHR，不需要窗口句柄继续有效。
 */
JNIEXPORT jint JNICALL
Java_com_geoscatter3d_ui_1flutter_MainActivity_nativeAttachSurface(
    JNIEnv* env,
    jobject /* thiz */,
    jlong engine_handle,
    jobject surface,
    jint width,
    jint height
) {
    if (engine_handle == 0 || surface == nullptr || env == nullptr) {
        return -1;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return -2;
    }

    gs3d_surface_provider provider{};
    provider.instance_extensions = kAndroidSurfaceExtensions;
    provider.instance_extension_count = 1;
    provider.create_surface = &create_android_surface;
    provider.user_data = window;

    const int32_t result = gs3d_engine_set_surface(
        reinterpret_cast<gs3d_engine*>(engine_handle),
        &provider,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    );

    ANativeWindow_release(window);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_geoscatter3d_ui_1flutter_MainActivity_nativePresentFrame(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jlong engine_handle
) {
    if (engine_handle == 0) {
        return -1;
    }
    return gs3d_engine_present_frame(
        reinterpret_cast<gs3d_engine*>(engine_handle));
}

JNIEXPORT jint JNICALL
Java_com_geoscatter3d_ui_1flutter_MainActivity_nativeClearSurface(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jlong engine_handle
) {
    if (engine_handle == 0) {
        return -1;
    }
    return gs3d_engine_clear_surface(
        reinterpret_cast<gs3d_engine*>(engine_handle));
}

JNIEXPORT jint JNICALL
Java_com_geoscatter3d_ui_1flutter_MainActivity_nativeHasSurface(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jlong engine_handle
) {
    if (engine_handle == 0) {
        return 0;
    }
    return gs3d_engine_has_surface(
        reinterpret_cast<gs3d_engine*>(engine_handle));
}

}  // extern "C"

#endif  // __ANDROID__
