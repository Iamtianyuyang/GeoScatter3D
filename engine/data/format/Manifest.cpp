#include "data/format/Manifest.hpp"

#include "foundation/io/AtomicFile.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace gs3d::data {
namespace {

using json = nlohmann::json;

const char* const kKnownFeatures[] = {"tiles"};

const char* const kKnownChunkPrefixes[] = {"points/", "lod/", "tiles/"};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("manifest: " + message);
}

const json& require(const json& node, const char* key, const char* where) {
    const auto it = node.find(key);
    if (it == node.end()) {
        fail(std::string("missing \"") + key + "\" in " + where);
    }
    return *it;
}

std::string read_string(const json& node, const char* key, const char* where) {
    const json& value = require(node, key, where);
    if (!value.is_string()) {
        fail(std::string("\"") + key + "\" in " + where + " must be a string");
    }
    return value.get<std::string>();
}

double read_number(const json& node, const char* key, const char* where) {
    const json& value = require(node, key, where);
    if (!value.is_number()) {
        fail(std::string("\"") + key + "\" in " + where + " must be a number");
    }
    return value.get<double>();
}

std::uint64_t read_u64(const json& node, const char* key, const char* where) {
    const double value = read_number(node, key, where);
    if (value < 0.0 || value > 18446744073709551615.0) {
        fail(std::string("\"") + key + "\" in " + where + " is out of range");
    }
    return static_cast<std::uint64_t>(value);
}

std::array<double, 3> read_vec3(const json& node, const char* key, const char* where) {
    const json& value = require(node, key, where);
    if (!value.is_array() || value.size() != 3) {
        fail(std::string("\"") + key + "\" in " + where + " must be an array of 3 numbers");
    }
    std::array<double, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!value[i].is_number()) {
            fail(std::string("\"") + key + "\" in " + where + " must hold numbers");
        }
        out[i] = value[i].get<double>();
    }
    return out;
}

bool known_chunk_path(const std::string& path) {
    return std::any_of(std::begin(kKnownChunkPrefixes), std::end(kKnownChunkPrefixes),
                       [&](const char* prefix) { return path.rfind(prefix, 0) == 0; });
}

std::uint32_t parse_hex32(const json& node, const char* where) {
    if (!node.is_string()) {
        fail(std::string("chunk crc32c in ") + where + " must be a hex string");
    }
    const std::string text = node.get<std::string>();
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 16);
    if (end == text.c_str()) {
        fail(std::string("chunk crc32c in ") + where + " is not hexadecimal");
    }
    return static_cast<std::uint32_t>(value);
}

std::string format_hex32(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
    return buffer;
}

} // namespace

std::string serialize_manifest(const Manifest& manifest) {
    json root;
    root["format_version"] = manifest.format_version;
    root["min_reader_version"] = manifest.min_reader_version;
    root["required_features"] = manifest.required_features;

    json crs;
    crs["authority"] = manifest.crs.authority;
    crs["code"] = manifest.crs.code;
    crs["axis_order"] = manifest.crs.axis_order;
    crs["unit"] = manifest.crs.unit;
    crs["vertical_unit"] = manifest.crs.vertical_unit;
    crs["datum"] = manifest.crs.datum;
    root["crs"] = std::move(crs);

    root["origin"] = manifest.origin;
    root["bounds"] = {{"min", manifest.bounds_min}, {"max", manifest.bounds_max}};
    root["points"] = {{"count", manifest.point_count}, {"stride", manifest.stride}};

    json attributes = json::array();
    for (const ManifestAttribute& attribute : manifest.attributes) {
        json item;
        item["name"] = attribute.name;
        item["type"] = attribute.type;
        item["unit"] = attribute.unit;
        if (attribute.has_null) {
            item["null"] = attribute.null_value;
        }
        attributes.push_back(std::move(item));
    }
    root["attributes"] = std::move(attributes);

    json chunks = json::array();
    for (const ManifestChunk& chunk : manifest.chunks) {
        chunks.push_back({{"path", chunk.path},
                          {"points", chunk.points},
                          {"bytes", chunk.bytes},
                          {"crc32c", format_hex32(chunk.crc32c)}});
    }
    root["chunks"] = std::move(chunks);

    root["job"] = {{"id", manifest.job.id},
                   {"source",
                    {{"path", manifest.job.source_path},
                     {"bytes", manifest.job.source_bytes},
                     {"sha256", manifest.job.source_sha256}}},
                   {"options_hash", manifest.job.options_hash},
                   {"stages", manifest.job.stages}};

    return root.dump(2) + "\n";
}

Manifest parse_manifest(std::string_view json_text) {
    json root;
    try {
        root = json::parse(json_text.begin(), json_text.end());
    } catch (const json::exception& error) {
        fail(std::string("invalid JSON: ") + error.what());
    }
    if (!root.is_object()) {
        fail("root must be an object");
    }

    Manifest manifest;
    manifest.format_version = static_cast<int>(read_u64(root, "format_version", "root"));
    if (manifest.format_version != Manifest::kFormatVersion) {
        fail("unsupported format_version " + std::to_string(manifest.format_version));
    }
    manifest.min_reader_version = static_cast<int>(read_u64(root, "min_reader_version", "root"));
    if (manifest.min_reader_version > Manifest::kFormatVersion) {
        fail("bundle needs reader version " + std::to_string(manifest.min_reader_version) +
             ", this build reads at most " + std::to_string(Manifest::kFormatVersion));
    }

    const json& features = require(root, "required_features", "root");
    if (!features.is_array()) {
        fail("\"required_features\" must be an array");
    }
    for (const json& feature : features) {
        if (!feature.is_string()) {
            fail("\"required_features\" must hold strings");
        }
        const std::string name = feature.get<std::string>();
        const bool known = std::any_of(std::begin(kKnownFeatures), std::end(kKnownFeatures),
                                       [&](const char* candidate) { return name == candidate; });
        if (!known) {
            fail("unknown required feature \"" + name + "\"");
        }
        manifest.required_features.push_back(name);
    }

    if (root.contains("crs") && root["crs"].is_object()) {
        const json& crs = root["crs"];
        manifest.crs.authority = crs.value("authority", std::string{});
        manifest.crs.code = crs.value("code", 0);
        manifest.crs.axis_order = crs.value("axis_order", std::string{});
        manifest.crs.unit = crs.value("unit", std::string{});
        manifest.crs.vertical_unit = crs.value("vertical_unit", std::string{});
        manifest.crs.datum = crs.value("datum", std::string{});
    }

    manifest.origin = read_vec3(root, "origin", "root");
    const json& bounds = require(root, "bounds", "root");
    if (!bounds.is_object()) {
        fail("\"bounds\" must be an object");
    }
    manifest.bounds_min = read_vec3(bounds, "min", "bounds");
    manifest.bounds_max = read_vec3(bounds, "max", "bounds");
    for (std::size_t i = 0; i < 3; ++i) {
        if (manifest.bounds_min[i] > manifest.bounds_max[i]) {
            fail("bounds min exceeds max");
        }
    }

    const json& points = require(root, "points", "root");
    if (!points.is_object()) {
        fail("\"points\" must be an object");
    }
    manifest.point_count = read_u64(points, "count", "points");
    manifest.stride = static_cast<std::uint32_t>(read_u64(points, "stride", "points"));
    if (manifest.stride == 0) {
        fail("\"stride\" must be positive");
    }

    if (root.contains("attributes")) {
        if (!root["attributes"].is_array()) {
            fail("\"attributes\" must be an array");
        }
        for (const json& item : root["attributes"]) {
            if (!item.is_object()) {
                fail("attribute entries must be objects");
            }
            ManifestAttribute attribute;
            attribute.name = read_string(item, "name", "attribute");
            attribute.type = read_string(item, "type", "attribute");
            attribute.unit = item.value("unit", std::string{});
            if (item.contains("null") && item["null"].is_number()) {
                attribute.has_null = true;
                attribute.null_value = item["null"].get<double>();
            }
            manifest.attributes.push_back(std::move(attribute));
        }
    }

    if (root.contains("chunks")) {
        if (!root["chunks"].is_array()) {
            fail("\"chunks\" must be an array");
        }
        for (const json& item : root["chunks"]) {
            if (!item.is_object()) {
                fail("chunk entries must be objects");
            }
            const std::string path = read_string(item, "path", "chunk");
            if (!known_chunk_path(path)) {
                continue; // unknown optional chunk kind: ignore, never fail
            }
            ManifestChunk chunk;
            chunk.path = path;
            chunk.points = read_u64(item, "points", "chunk");
            chunk.bytes = read_u64(item, "bytes", "chunk");
            chunk.crc32c = parse_hex32(require(item, "crc32c", "chunk"), "chunk");
            manifest.chunks.push_back(std::move(chunk));
        }
    }

    if (root.contains("job") && root["job"].is_object()) {
        const json& job = root["job"];
        manifest.job.id = job.value("id", std::string{});
        manifest.job.options_hash = job.value("options_hash", std::string{});
        if (job.contains("source") && job["source"].is_object()) {
            const json& source = job["source"];
            manifest.job.source_path = source.value("path", std::string{});
            manifest.job.source_bytes = source.value("bytes", std::uint64_t{0});
            manifest.job.source_sha256 = source.value("sha256", std::string{});
        }
        if (job.contains("stages") && job["stages"].is_object()) {
            for (auto it = job["stages"].begin(); it != job["stages"].end(); ++it) {
                if (it.value().is_string()) {
                    manifest.job.stages[it.key()] = it.value().get<std::string>();
                }
            }
        }
    }

    return manifest;
}

Manifest load_bundle_manifest(const std::filesystem::path& bundle_dir) {
    const std::filesystem::path path = bundle_dir / "manifest.json";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("manifest: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return parse_manifest(buffer.str());
}

void save_bundle_manifest(const std::filesystem::path& bundle_dir, const Manifest& manifest) {
    std::error_code ec;
    std::filesystem::create_directories(bundle_dir, ec);
    if (ec) {
        throw std::runtime_error("manifest: cannot create " + bundle_dir.string() + ": " +
                                 ec.message());
    }
    foundation::atomic_write_file(bundle_dir / "manifest.json", serialize_manifest(manifest));
}

} // namespace gs3d::data
