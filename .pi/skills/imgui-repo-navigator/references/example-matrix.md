# Dear ImGui Example Matrix

## Cross-platform examples

- `examples/example_glfw_opengl2`: GLFW + OpenGL 2
- `examples/example_glfw_opengl3`: GLFW + OpenGL 3
- `examples/example_glfw_vulkan`: GLFW + Vulkan
- `examples/example_glfw_metal`: GLFW + Metal
- `examples/example_glfw_wgpu`: GLFW + WebGPU
- `examples/example_sdl2_opengl2`: SDL2 + OpenGL 2
- `examples/example_sdl2_opengl3`: SDL2 + OpenGL 3
- `examples/example_sdl2_sdlrenderer2`: SDL2 + SDL_Renderer
- `examples/example_sdl2_vulkan`: SDL2 + Vulkan
- `examples/example_sdl2_wgpu`: SDL2 + WebGPU
- `examples/example_sdl3_opengl3`: SDL3 + OpenGL 3
- `examples/example_sdl3_sdlrenderer3`: SDL3 + SDL_Renderer
- `examples/example_sdl3_sdlgpu3`: SDL3 + SDL GPU
- `examples/example_sdl3_vulkan`: SDL3 + Vulkan
- `examples/example_sdl3_wgpu`: SDL3 + WebGPU

## Windows-native examples

- `examples/example_win32_directx9`: Win32 + DirectX 9
- `examples/example_win32_directx10`: Win32 + DirectX 10
- `examples/example_win32_directx11`: Win32 + DirectX 11
- `examples/example_win32_directx12`: Win32 + DirectX 12
- `examples/example_win32_opengl3`: Win32 + OpenGL 3
- `examples/example_win32_vulkan`: Win32 + Vulkan
- `examples/example_sdl2_directx11`: SDL2 + DirectX 11
- `examples/example_sdl3_directx11`: SDL3 + DirectX 11

## Apple / mobile / special cases

- `examples/example_apple_opengl2`: macOS native + OpenGL 2
- `examples/example_apple_metal`: macOS native + Metal
- `examples/example_android_opengl3`: Android native + OpenGL ES 3
- `examples/example_allegro5`: single Allegro 5 backend
- `examples/example_glut_opengl2`: GLUT + OpenGL 2
- `examples/example_null`: no renderer, minimal headless skeleton

## Selection rules

1. Prefer an exact platform + renderer match.
2. If there is no exact match, preserve the platform first for window, input, and lifecycle details.
3. Preserve the renderer second for draw-data upload, texture ownership, and device-loss details.
4. Use example `main.cpp` for orchestration and backend headers for callable entry points.
