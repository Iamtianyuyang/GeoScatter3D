#include "app/ViewerDatasetDescriptor.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace gs3d::app {

gs3d::core::DatasetDescriptor make_viewer_dataset_descriptor(
    const gs3d::data::Gs3dDataset& dataset,
    const std::filesystem::path& source_path,
    const std::vector<AttrDescriptor>& attributes
) {
    gs3d::core::DatasetDescriptor descriptor;
    descriptor.display_name = source_path.filename().string();
    // Bundle 项目内部的数据文件统一叫 source.gs3d。项目 UI 应展示
    // bundle 目录名，并去掉约定的 .gs3d.bundle 双后缀。
    if (source_path.filename() == "source.gs3d") {
        auto project_name = source_path.parent_path().filename();
        if (project_name.extension() == ".bundle") {
            project_name = project_name.stem();
        }
        if (project_name.extension() == ".gs3d") {
            project_name = project_name.stem();
        }
        if (!project_name.empty()) {
            descriptor.display_name = project_name.string();
        }
    }
    descriptor.path = source_path.string();
    descriptor.format = "GS3D";
    descriptor.point_count = dataset.point_count();
    descriptor.bounds = {
        dataset.bbox_min_x(),
        dataset.bbox_min_y(),
        dataset.bbox_min_z(),
        dataset.bbox_max_x(),
        dataset.bbox_max_y(),
        dataset.bbox_max_z()
    };
    descriptor.dataset_tree = {
        descriptor.display_name,
        "瓦片",
        "细节层级",
        "属性"
    };
    descriptor.attributes.reserve(attributes.size());
    for (const auto& attribute : attributes) {
        descriptor.attributes.push_back({attribute.name});
    }

    std::error_code error;
    const auto file_bytes = std::filesystem::file_size(source_path, error);
    if (!error) {
        std::ostringstream output;
        output << std::fixed << std::setprecision(2)
               << static_cast<double>(file_bytes) / (1024.0 * 1024.0)
               << " MB";
        descriptor.file_size = output.str();
    }
    return descriptor;
}

} // namespace gs3d::app
