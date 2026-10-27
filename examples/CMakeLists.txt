cmake_minimum_required(VERSION 3.25)
project(vk_triangle CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Toolchain roots (zero-vcpkg). CMAKE_SOURCE_DIR is the engine root.
set(VK_ROOT "C:/VulkanSDK/1.4.363.0" CACHE PATH "Vulkan SDK root")
set(SDL3_ROOT "${CMAKE_SOURCE_DIR}/../SDL3_3418/x86_64-w64-mingw32" CACHE PATH "SDL3 root")

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vk_triangle")
set(OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

add_custom_command(
    OUTPUT "${OUT_DIR}/triangle.vert.spv"
    COMMAND "${VK_ROOT}/Bin/glslangValidator.exe" -V -S vert
        -o "${OUT_DIR}/triangle.vert.spv" "${SRC_DIR}/triangle.vert.glsl"
    DEPENDS "${SRC_DIR}/triangle.vert.glsl"
    COMMENT "Compiling triangle.vert.glsl -> SPIR-V")
add_custom_command(
    OUTPUT "${OUT_DIR}/triangle.frag.spv"
    COMMAND "${VK_ROOT}/Bin/glslangValidator.exe" -V -S frag
        -o "${OUT_DIR}/triangle.frag.spv" "${SRC_DIR}/triangle.frag.glsl"
    DEPENDS "${SRC_DIR}/triangle.frag.glsl"
    COMMENT "Compiling triangle.frag.glsl -> SPIR-V")

add_executable(vk_triangle
    "${SRC_DIR}/triangle.cpp"
    "${OUT_DIR}/triangle.vert.spv"
    "${OUT_DIR}/triangle.frag.spv")

target_include_directories(vk_triangle PRIVATE
    ${VK_ROOT}/Include
    ${SDL3_ROOT}/include)
target_link_libraries(vk_triangle PRIVATE
    "${VK_ROOT}/Lib/vulkan-1.lib"
    "${SDL3_ROOT}/lib/libSDL3.dll.a")

add_custom_command(TARGET vk_triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL3_ROOT}/bin/SDL3.dll" "${CMAKE_BINARY_DIR}/SDL3.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VK_ROOT}/Bin/vulkan-1.dll" "${CMAKE_BINARY_DIR}/vulkan-1.dll"
    COMMENT "Copying runtime DLLs next to exe")
