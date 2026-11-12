#pragma once

// Single source of truth: CMakeLists.txt parses this literal into
// project(VERSION), and both FFI layers return it. ui_flutter/pubspec.yaml
// keeps its own copy because Flutter tooling cannot read C++ headers.
#define GS3D_PRODUCT_VERSION "0.0.1000"
