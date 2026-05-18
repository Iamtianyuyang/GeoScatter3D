cmake_minimum_required(VERSION 3.28.3)

project(GeoScatter3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

add_executable(GeoScatter3D
    src/main.cpp

    src/platform/Window.cpp

    src/data/CsvStreamReader.cpp
    src/data/DataSchema.cpp
    src/data/Delimiter.cpp
    src/data/Gs3dFormat.cpp
    src/data/Gs3dReader.cpp
    src/data/Gs3dReader.cpp
    src/data/Gs3dVerifier.cpp
    src/data/Gs3dDataset.cpp

    src/camera/Camera.cpp
    src/camera/CameraController.cpp
    src/camera/MouseRay.cpp
    
    src/preprocess/StatisticsPass.cpp
    src/preprocess/Gs3dWriter.cpp
    
    src/render/VulkanContext.cpp
    src/render/VulkanSwapchain.cpp
    src/render/VulkanRenderer.cpp
    src/render/VulkanBuffer.cpp
    src/render/PointCloudGpu.cpp
    src/render/PointPipeline.cpp
    src/render/VulkanDepthBuffer.cpp

    include/platform/Window.hpp

    include/data/Gs3dVerifier.hpp
    include/data/CsvStreamReader.hpp
    include/data/DataSchema.hpp
    include/data/Delimiter.hpp
    include/data/Gs3dFormat.hpp
    include/data/Gs3dReader.hpp
    include/data/Gs3dDataset.hpp
    
    include/camera/Camera.hpp
    include/camera/CameraController.hpp
    include/camera/MouseRay.hpp

    include/preprocess/StatisticsPass.hpp
    include/preprocess/Gs3dWriter.hpp

    include/render/VulkanContext.hpp
    include/render/VulkanSwapchain.hpp
    include/render/VulkanRenderer.hpp
    include/render/VulkanBuffer.hpp
    include/render/PointCloudGpu.hpp  
    include/render/PointPipeline.hpp
    include/render/VulkanDepthBuffer.hpp

    
)

target_include_directories(GeoScatter3D PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(GeoScatter3D PRIVATE
    Vulkan::Vulkan
    glfw
)