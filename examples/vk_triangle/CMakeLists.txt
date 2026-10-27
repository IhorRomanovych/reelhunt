cmake_minimum_required(VERSION 3.25)
project(vk_triangle CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Authoritative Vulkan SDK (headers + glslangValidator).
set(VK_SDK_ROOT "C:/VulkanSDK/1.4.363.0")
set(VK_SHADERC "${VK_SDK_ROOT}/Bin/glslangValidator.exe")
set(VK_SDK_INCLUDE "${VK_SDK_ROOT}/Include")

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)

# Directory-scope include dir is searched before target include dirs (the
# vcpkg-bundled Vulkan headers), so the SDK's vulkan/ headers are authoritative.
include_directories(${VK_SDK_INCLUDE})

message(STATUS "VK_SHADERC=${VK_SHADERC}")
message(STATUS "VK_SDK_INCLUDE=${VK_SDK_INCLUDE}")
message(STATUS "SDL2_DIR=${SDL2_DIR}")
message(STATUS "SDL2_INCLUDE_DIRS=${SDL2_INCLUDE_DIRS}")
message(STATUS "Vulkan_INCLUDE_DIR=${Vulkan_INCLUDE_DIR}")
message(STATUS "Vulkan_LIBRARY=${Vulkan_LIBRARY}")

set(OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

add_custom_command(
  OUTPUT "${OUT_DIR}/triangle.vert.spv"
  COMMAND "${VK_SHADERC}" -V -S vert
    -o "${OUT_DIR}/triangle.vert.spv" "${CMAKE_CURRENT_SOURCE_DIR}/triangle.vert.glsl"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/triangle.vert.glsl"
  COMMENT "Compiling triangle.vert.glsl -> SPIR-V")

add_custom_command(
  OUTPUT "${OUT_DIR}/triangle.frag.spv"
  COMMAND "${VK_SHADERC}" -V -S frag
    -o "${OUT_DIR}/triangle.frag.spv" "${CMAKE_CURRENT_SOURCE_DIR}/triangle.frag.glsl"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/triangle.frag.glsl"
  COMMENT "Compiling triangle.frag.glsl -> SPIR-V")

add_executable(vk_triangle
  "${CMAKE_CURRENT_SOURCE_DIR}/triangle.cpp"
  "${OUT_DIR}/triangle.vert.spv"
  "${OUT_DIR}/triangle.frag.spv")

target_link_libraries(vk_triangle PRIVATE
  Vulkan::Vulkan
  ${SDL2_LIBRARIES})
target_include_directories(vk_triangle PRIVATE
  ${SDL2_INCLUDE_DIRS})

# Copy SDL2 DLLs next to the executable so it can load them at runtime.
find_path(SDL2_DLL_DIR
  NAMES SDL2.dll
  PATHS "${SDL2_BINDIR}"
  FILE_TYPE DIRECTORY)
if(SDL2_DLL_DIR)
  add_custom_command(TARGET vk_triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${SDL2_DLL_DIR}/SDL2.dll" "${CMAKE_BINARY_DIR}/examples/vk_triangle/SDL2.dll"
    COMMENT "Copying SDL2.dll")
endif()
