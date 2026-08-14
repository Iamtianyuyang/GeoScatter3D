#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace gs3d::ui::icons {

struct Glyph { const char* name; std::uint32_t codepoint; };

inline constexpr Glyph kFolderOpen{"folder_open", 0xE2C8};
inline constexpr Glyph kAdd{"add", 0xE145};
inline constexpr Glyph kPhotoCamera{"photo_camera", 0xE412};
inline constexpr Glyph kStraighten{"straighten", 0xE41C};
inline constexpr Glyph kLink{"link", 0xE157};
inline constexpr Glyph kPalette{"palette", 0xE40A};
inline constexpr Glyph kKeyboard{"keyboard", 0xE312};
inline constexpr Glyph kTune{"tune", 0xE429};
inline constexpr Glyph kMonitor{"monitor", 0xE322};
inline constexpr Glyph kInfo{"info", 0xE88E};
inline constexpr Glyph kClose{"close", 0xE5CD};
inline constexpr Glyph kSearch{"search", 0xE8B6};
inline constexpr Glyph kRefresh{"refresh", 0xE5D5};
inline constexpr Glyph kCheck{"check", 0xE5CA};
inline constexpr Glyph kSettings{"settings", 0xE8B8};
inline constexpr Glyph kLayers{"layers", 0xE53B};
inline constexpr Glyph kAssessment{"assessment", 0xE85C};
inline constexpr Glyph kMap{"map", 0xE55B};
inline constexpr Glyph kDashboard{"dashboard", 0xE871};
inline constexpr Glyph kVisibility{"visibility", 0xE8F4};
inline constexpr Glyph kHome{"home", 0xE88A};
inline constexpr Glyph kMenu{"menu", 0xE5D2};
inline constexpr Glyph kMoreVert{"more_vert", 0xE5D4};
inline constexpr Glyph kContentCopy{"content_copy", 0xE14D};
inline constexpr Glyph kDelete{"delete", 0xE872};
inline constexpr Glyph kSave{"save", 0xE161};
inline constexpr Glyph kEdit{"edit", 0xE150};
inline constexpr Glyph kTableChart{"table_chart", 0xE9D4};
inline constexpr Glyph kCloudDone{"cloud_done", 0xE2BF};
inline constexpr Glyph kMemory{"memory", 0xE322};
inline constexpr Glyph kSpeed{"speed", 0xE9E4};
inline constexpr Glyph kViewModule{"view_module", 0xE8F0};
inline constexpr Glyph kImage{"image", 0xE3F4};
inline constexpr Glyph kTimeline{"timeline", 0xE925};
inline constexpr Glyph kOpenInNew{"open_in_new", 0xE89E};
inline constexpr Glyph kApps{"apps", 0xE5C3};
inline constexpr Glyph kCheckCircle{"check_circle", 0xE86C};
inline constexpr Glyph kUpdate{"update", 0xE923};
inline constexpr Glyph kPublic{"public", 0xE80B};
inline constexpr Glyph kExplore{"explore", 0xE87F};
inline constexpr Glyph kSelectAll{"select_all", 0xE162};
inline constexpr Glyph kPanoramaFishEye{"panorama_fish_eye", 0xE3EA};
inline constexpr Glyph kFullscreen{"fullscreen", 0xE5D0};
inline constexpr Glyph kExpandMore{"expand_more", 0xE5CF};
inline constexpr Glyph kExpandLess{"expand_less", 0xE5CE};
inline constexpr Glyph kArrowUpward{"arrow_upward", 0xE5D8};
inline constexpr Glyph kArrowDownward{"arrow_downward", 0xE5DB};
inline constexpr Glyph kScreenRotation{"screen_rotation", 0xE91D};
inline constexpr Glyph kZoomIn{"zoom_in", 0xE8FF};
inline constexpr Glyph kZoomOut{"zoom_out", 0xE900};
inline constexpr Glyph k3DRotation{"3d_rotation", 0xE84D};

inline constexpr std::array<const Glyph*, 51> kAll{{
    &kFolderOpen, &kAdd, &kPhotoCamera, &kStraighten, &kLink,
    &kPalette, &kKeyboard, &kTune, &kMonitor, &kInfo,
    &kClose, &kSearch, &kRefresh, &kCheck, &kSettings,
    &kLayers, &kAssessment, &kMap, &kDashboard, &kVisibility,
    &kHome, &kMenu, &kMoreVert, &kContentCopy, &kDelete,
    &kSave, &kEdit, &kTableChart, &kCloudDone, &kMemory,
    &kSpeed, &kViewModule, &kImage, &kTimeline, &kOpenInNew,
    &kApps, &kCheckCircle, &kUpdate, &kPublic, &kExplore,
    &kSelectAll, &kPanoramaFishEye, &kFullscreen, &kExpandMore,
    &kExpandLess, &kArrowUpward, &kArrowDownward, &kScreenRotation,
    &kZoomIn, &kZoomOut, &k3DRotation,
}};

[[nodiscard]]
inline const char* utf8(std::uint32_t cp, char (&buf)[5]) noexcept
{
    if (cp <= 0x7F) { buf[0] = (char)cp; buf[1] = '\0'; }
    else if (cp <= 0x7FF) { buf[0] = (char)(0xC0|(cp>>6)); buf[1] = (char)(0x80|(cp&0x3F)); buf[2] = '\0'; }
    else if (cp <= 0xFFFF) { buf[0] = (char)(0xE0|(cp>>12)); buf[1] = (char)(0x80|((cp>>6)&0x3F)); buf[2] = (char)(0x80|(cp&0x3F)); buf[3] = '\0'; }
    else { buf[0] = (char)(0xF0|(cp>>18)); buf[1] = (char)(0x80|((cp>>12)&0x3F)); buf[2] = (char)(0x80|((cp>>6)&0x3F)); buf[3] = (char)(0x80|(cp&0x3F)); buf[4] = '\0'; }
    return buf;
}

[[nodiscard]]
inline std::string utf8(std::uint32_t cp) { char b[5]; return utf8(cp, b); }

} // namespace gs3d::ui::icons
