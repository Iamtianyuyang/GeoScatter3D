cmake_minimum_required(VERSION 3.28.3)

project(GeoScatter3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Threads REQUIRED)

add_executable(GeoScatter3D
    src/main.cpp

    src/app/ViewerApp.cpp
    src/app/AppConfig.cpp
    src/app/ResourcePath.cpp

    src/platform/Window.cpp

    src/data/CsvStreamReader.cpp
    src/data/DataSchema.cpp
    src/data/Delimiter.cpp
    src/data/Gs3dFormat.cpp
    src/data/Gs3dReader.cpp
    src/data/Gs3dReader.cpp
    src/data/Gs3dVerifier.cpp
    src/data/Gs3dDataset.cpp
    src/data/Gs3dLodDataset.cpp
    src/data/Gs3dLodFormat.cpp
    src/data/Gs3dLodReader.cpp
    src/data/Gs3dTileFormat.cpp
    src/data/Gs3dTileReader.cpp

    src/camera/Camera.cpp
    src/camera/CameraController.cpp
    src/camera/MouseRay.cpp

    src/preprocess/StatisticsPass.cpp
    src/preprocess/Gs3dWriter.cpp
    src/preprocess/Gs3dLodWriter.cpp
    src/preprocess/Gs3dTileWriter.cpp

    src/render/VulkanContext.cpp
    src/render/VulkanSwapchain.cpp
    src/render/VulkanRenderer.cpp
    src/render/VulkanBuffer.cpp
    src/render/PointCloudGpu.cpp
    src/render/PointPipeline.cpp
    src/render/VulkanDepthBuffer.cpp
    src/render/PointCloudLodGpu.cpp
    src/render/LodSelector.cpp
    src/render/PointCloudTileGpu.cpp
    src/render/TileSelection.cpp

    include/app/ViewerApp.hpp
    include/app/AppConfig.hpp
    include/app/ResourcePath.hpp

    include/platform/Window.hpp

    include/data/Gs3dVerifier.hpp
    include/data/CsvStreamReader.hpp
    include/data/DataSchema.hpp
    include/data/Delimiter.hpp
    include/data/Gs3dFormat.hpp
    include/data/Gs3dReader.hpp
    include/data/Gs3dDataset.hpp
    include/data/Gs3dLodDataset.hpp
    include/data/Gs3dLodFormat.hpp
    include/data/Gs3dLodReader.hpp
    include/data/Gs3dTileFormat.hpp
    include/data/Gs3dTileReader.hpp

    include/camera/Camera.hpp
    include/camera/CameraController.hpp
    include/camera/MouseRay.hpp

    include/preprocess/StatisticsPass.hpp
    include/preprocess/Gs3dWriter.hpp
    include/preprocess/Gs3dLodWriter.hpp  
    include/preprocess/Gs3dTileWriter.hpp

    include/render/VulkanContext.hpp
    include/render/VulkanSwapchain.hpp
    include/render/VulkanRenderer.hpp
    include/render/VulkanBuffer.hpp
    include/render/PointCloudGpu.hpp  
    include/render/PointPipeline.hpp
    include/render/VulkanDepthBuffer.hpp
    include/render/PointCloudLodGpu.hpp
    include/render/LodSelector.hpp
    include/render/PointCloudTileGpu.hpp
    include/render/TileSelection.hpp

    include/util/ThreadPool.hpp
)

target_include_directories(GeoScatter3D PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
)

target_link_libraries(GeoScatter3D PRIVATE
    Vulkan::Vulkan
    glfw
    Threads::Threads
)
