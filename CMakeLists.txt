cmake_minimum_required(VERSION 3.25)
project(Reelhunt C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# --- Options ---
option(REELHUNT_BUILD_TESTS "Build the test executables (ctest)" ON)
option(REELHUNT_BUILD_TOOLS "Build the dev dump tools (txd_dump, mls_dump, ...)" ON)
option(REELHUNT_BUILD_EXAMPLES "Build examples/vk_triangle (Windows SDK layout only)" ${WIN32})
option(REELHUNT_FETCH_SDL3 "Download and build SDL3 when no SDL3 package is found" ON)
set(REELHUNT_GAME_ROOT "${CMAKE_SOURCE_DIR}/../game/" CACHE PATH
    "Development fallback for the game data root (installed builds find the game next to themselves; override at runtime with --game-root or REELHUNT_GAME_ROOT)")

# --- librw (aap/librw, NULL platform: parse-only), shared by the engine and the tests ---
# A prebuilt checkout next to this tree is used when present (LIBRW_SRC_DIR/_build/src/librw.a);
# otherwise the pinned commit is fetched, patched (cmake/patches) and built as part of this project.
set(LIBRW_SRC_DIR "${CMAKE_SOURCE_DIR}/../librw" CACHE PATH "librw source root (optional prebuilt checkout)")
set(LIBRW_GIT_TAG "18532c2e13efbc1aa43f0b00b4459831f9414de0" CACHE STRING "librw commit to fetch")
if(EXISTS "${LIBRW_SRC_DIR}/_build/src/librw.a")
    add_library(librw STATIC IMPORTED GLOBAL)
    set_target_properties(librw PROPERTIES
        IMPORTED_LOCATION "${LIBRW_SRC_DIR}/_build/src/librw.a"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${LIBRW_SRC_DIR}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBRW_SRC_DIR}")
    message(STATUS "librw: prebuilt ${LIBRW_SRC_DIR}/_build/src/librw.a")
else()
    set(LIBRW_PLATFORM "NULL" CACHE STRING "librw platform" FORCE)
    set(LIBRW_TOOLS OFF CACHE BOOL "" FORCE)
    set(LIBRW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(librw_src
        GIT_REPOSITORY https://github.com/aap/librw.git
        GIT_TAG ${LIBRW_GIT_TAG}
        PATCH_COMMAND git apply --whitespace=nowarn "${CMAKE_SOURCE_DIR}/cmake/patches/librw-null-raster.patch"
        UPDATE_DISCONNECTED ON)
    FetchContent_MakeAvailable(librw_src)
    # librw's own target is librw::librw (or librw); expose it under one name.
    if(NOT TARGET librw)
        add_library(librw ALIAS librw::librw)
    endif()
    message(STATUS "librw: fetched ${LIBRW_GIT_TAG}")
endif()

# --- GLM (header-only math): the system package when it is new enough, else a pinned release ---
set(REELHUNT_GLM_TAG "1.0.3" CACHE STRING "GLM release to fetch when no system package is found")
find_package(glm 1.0.1 CONFIG QUIET)
if(glm_FOUND AND TARGET glm::glm)
    message(STATUS "GLM: system package ${glm_VERSION}")
else()
    # Only the headers are needed: SOURCE_SUBDIR points at a folder without a CMakeLists.txt, so
    # GLM's own build (its library, its compile options) is not added.
    FetchContent_Declare(glm_src
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG ${REELHUNT_GLM_TAG}
        GIT_SHALLOW ON
        SOURCE_SUBDIR headers-only)
    FetchContent_MakeAvailable(glm_src)
    add_library(glm_headers INTERFACE)
    target_include_directories(glm_headers SYSTEM INTERFACE "${glm_src_SOURCE_DIR}")
    add_library(glm::glm ALIAS glm_headers)
    message(STATUS "GLM: fetched ${REELHUNT_GLM_TAG}")
endif()

# --- Platform front end: SDL3 (window, input), Vulkan (rendering), OpenAL (audio) ---
if(WIN32)
    # Zero-vcpkg Windows layout: SDKs unpacked next to the source tree (see docs/BUILDING.md).
    set(VK_ROOT "C:/VulkanSDK/1.4.363.0" CACHE PATH "Vulkan SDK root")
    set(SDL3_ROOT "${CMAKE_SOURCE_DIR}/../SDL3_3418/x86_64-w64-mingw32" CACHE PATH "SDL3 root")
    set(OAL_ROOT "${CMAKE_SOURCE_DIR}/../OpenAl_SDK" CACHE PATH "OpenAL SDK root")
    set(GLSLANG_VALIDATOR "${VK_ROOT}/Bin/glslangValidator.exe")
    # MinGW runtime DLLs live next to the compiler.
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_library(platform_deps INTERFACE)
    target_include_directories(platform_deps INTERFACE
        ${VK_ROOT}/Include ${SDL3_ROOT}/include ${OAL_ROOT}/include)
    target_link_libraries(platform_deps INTERFACE
        "${SDL3_ROOT}/lib/libSDL3.dll.a"
        "${VK_ROOT}/Lib/vulkan-1.lib"
        "${OAL_ROOT}/libs/Win64/OpenAL32.lib"
        gdi32) # gdi32: the launcher's banner bitmap
else()
    find_package(Vulkan REQUIRED COMPONENTS glslangValidator)
    find_package(OpenAL REQUIRED)
    find_package(Threads REQUIRED)
    set(GLSLANG_VALIDATOR "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        if(NOT REELHUNT_FETCH_SDL3)
            message(FATAL_ERROR "SDL3 not found: install your distribution's SDL3 development package or configure with -DREELHUNT_FETCH_SDL3=ON")
        endif()
        message(STATUS "SDL3: no package found, fetching release-3.2.24 (static)")
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3_src
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.2.24
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(SDL3_src)
    endif()
    add_library(platform_deps INTERFACE)
    target_link_libraries(platform_deps INTERFACE SDL3::SDL3 Vulkan::Vulkan OpenAL::OpenAL Threads::Threads ${CMAKE_DL_LIBS})
endif()

# --- Warning flags (our own targets only: set after the fetched dependencies) ---
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
endif()

# Runtime DLLs next to a Windows executable (no-op elsewhere).
function(copy_runtime_dlls target)
    if(WIN32)
        set(dest "$<TARGET_FILE_DIR:${target}>")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN}/libgcc_s_seh-1.dll" "${dest}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN}/libstdc++-6.dll" "${dest}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN}/libwinpthread-1.dll" "${dest}"
            COMMENT "Copying MinGW runtime DLLs next to ${target}")
    endif()
endfunction()

# --- engine_core: everything except main.cpp and the SDL/Vulkan/OpenAL front end ---
# Pure engine code (parsers, math, game logic) that the tests link against.
add_library(engine_core STATIC
    Assets/binary_reader.cpp
    Assets/col.cpp
    Assets/dff.cpp
    Assets/font.cpp
    Assets/grf.cpp
    Assets/gxt.cpp
    Assets/ifp/ifp.cpp
    Assets/ini_records.cpp
    Assets/inst.cpp
    Assets/librw_engine.cpp
    Assets/mls.cpp
    Assets/pak.cpp
    Assets/rib.cpp
    Assets/sfx_bank.cpp
    Assets/txd.cpp
    Assets/world.cpp
    Codec/bink_audio.cpp
    Codec/bink_container.cpp
    Codec/bink_decoder.cpp
    Codec/binkdsp.cpp
    Core/object.cpp
    Core/platform.cpp
    Game/ai/ai_director.cpp
    Game/ai/ai_natives.cpp
    Game/ai/ai_tuning.cpp
    Game/level_setup.cpp
    Game/game_text.cpp
    Game/items.cpp
    Game/execution.cpp
    Game/stamina.cpp
    Game/radar.cpp
    Game/weapon_strip.cpp
    Game/gunplay.cpp
    Game/pickup_halo.cpp
    Game/camera_splines.cpp
    Game/ai/nav_graph.cpp
    Game/ai/search.cpp
    Game/ai/melee_traits.cpp
    Game/video_modes.cpp
    Game/settings.cpp
    Game/level_score.cpp
    Game/progress.cpp
    Game/save_slots.cpp
    Game/checkpoint.cpp
    Game/ai/senses.cpp
    Game/ai/stealth.cpp
    Game/app_config.cpp
    Game/director_milestones.cpp
    Game/rats.cpp
    Game/crows.cpp
    Game/hand_cam.cpp
    Game/litter.cpp
    Game/crane.cpp
    Game/physics_props.cpp
    Game/doors.cpp
    Game/entity_audio.cpp
    Game/ped_anims.cpp
    Game/exe_tables.cpp
    Game/music_ambience.cpp
    Game/material_types.cpp
    Game/coronas.cpp
    Game/sample_levels.cpp
    Game/weapon_audio.cpp
    Game/acoustic_probe.cpp
    Game/player_audio.cpp
    Game/bindings.cpp
    Game/director_speech.cpp
    Game/hunter_speech.cpp
    Game/keymap.cpp
    Game/main_menu.cpp
    Game/scene_menu.cpp
    Game/save_menu.cpp
    Game/bonus_menu.cpp
    Game/settings_menu.cpp
    Game/pause_menu.cpp
    Game/results_menu.cpp
    Game/perf_overlay.cpp
    Game/melee_combo.cpp
    Game/player_controller.cpp
    Game/save_restore.cpp
    Game/save_restore_live.cpp
    Game/save_system.cpp
    Game/scenes.cpp
    Game/script/script_compiler.cpp
    Game/script/script_engine.cpp
    Game/locomotion.cpp
    Game/script_vm.cpp
    Game/texture_set.cpp
    Game/ui_text.cpp
    Physics/collider.cpp
    Physics/collision.cpp
    Physics/physics.cpp
    Physics/rigidbody.cpp
    Physics/triangle_field.cpp
    Renderer/camera.cpp
    Renderer/scene.cpp
    Renderer/shadow_cascades.cpp
    Renderer/texture.cpp)
target_include_directories(engine_core PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/GlobalContext)
target_link_libraries(engine_core PUBLIC librw glm::glm)
if(NOT WIN32)
    target_link_libraries(engine_core PUBLIC Threads::Threads)
endif()
target_compile_definitions(engine_core PRIVATE
    REELHUNT_DEFAULT_GAME_ROOT="${REELHUNT_GAME_ROOT}"
    REELHUNT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# --- main: the game executable (SDL3 window, Vulkan renderer, OpenAL audio) ---
add_executable(main
    main.cpp
    Audio/audio.cpp
    Core/asset_manager.cpp
    Game/anim_state.cpp
    Game/game_session.cpp
    Game/level_music.cpp
    Game/launcher.cpp
    Game/hud.cpp
    Game/bindings_sdl.cpp
    Game/gun_fx.cpp
    Game/fx.cpp
    Game/decals.cpp
    Game/hunter.cpp
    Game/level_runtime.cpp
    Game/character.cpp
    Game/cutscene_player.cpp
    Game/level_content.cpp
    Game/player_field.cpp
    Game/save_session.cpp
    Renderer/backend/composite_pass.cpp
    Renderer/backend/draw_list.cpp
    Renderer/backend/frame_resources.cpp
    Renderer/backend/forward_pass.cpp
    Renderer/backend/gpu_timer.cpp
    Renderer/backend/gbuffer_pass.cpp
    Renderer/backend/lamp_shadow_pass.cpp
    Renderer/backend/lighting_pass.cpp
    Renderer/backend/resource_cache.cpp
    Renderer/backend/shadow_pass.cpp
    Renderer/backend/vk_common.cpp
    Renderer/backend/vk_device.cpp
    Renderer/backend/vk_pipeline.cpp
    Renderer/backend/vulkan.cpp
    Renderer/fly_camera.cpp
    Renderer/renderer.cpp)
# The game executable is named Reelhunt(.exe): it can sit next to the original the exe and
# then finds the game data in its own folder.
set_target_properties(main PROPERTIES OUTPUT_NAME Reelhunt)
target_link_libraries(main PRIVATE engine_core platform_deps)

# --- Engine shaders -> SPIR-V (next to the executable; vulkan.cpp loads them from its own folder) ---
set(ENGINE_SHADERS
    gbuffer.vert skin.gbuffer.vert gbuffer.frag
    shadow.vert skin.shadow.vert shadow.frag shadowtint.frag
    lampshadow.vert skin.lampshadow.vert
    fullscreen.vert light.frag forward.frag composite.frag
    ui.vert ui.frag)
# Shared includes (#include "frame.glsl"): any change rebuilds every shader.
set(SHADER_INCLUDES
    "${CMAKE_SOURCE_DIR}/Renderer/backend/frame.glsl"
    "${CMAKE_SOURCE_DIR}/Renderer/backend/draw.glsl"
    "${CMAKE_SOURCE_DIR}/Renderer/backend/shading.glsl")
set(SHADER_OUTPUTS "")
foreach(shader IN LISTS ENGINE_SHADERS)
    string(REGEX MATCH "[^.]+$" stage "${shader}")
    set(src "${CMAKE_SOURCE_DIR}/Renderer/backend/${shader}.glsl")
    set(out "${CMAKE_BINARY_DIR}/${shader}.spv")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${GLSLANG_VALIDATOR}" -V -S ${stage} -o "${out}" "${src}"
        DEPENDS "${src}" ${SHADER_INCLUDES}
        COMMENT "glslang ${shader}.glsl"
        VERBATIM)
    list(APPEND SHADER_OUTPUTS "${out}")
endforeach()
add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(main shaders)

# The engine's data tables (config/) next to the executable, as an installed build has them.
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:main>/config"
    COMMENT "Copying config/ next to Reelhunt")
copy_runtime_dlls(main)
if(WIN32)
    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL3_ROOT}/bin/SDL3.dll" "$<TARGET_FILE_DIR:main>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VK_ROOT}/Bin/vulkan-1.dll" "$<TARGET_FILE_DIR:main>"
        COMMENT "Copying SDL3 + Vulkan loader next to Reelhunt")
endif()

# --- Install: a flat folder (executable, shaders, config/, runtime DLLs on Windows) meant to be
# copied into the game's folder, next to the exe (cmake --install <build> --prefix <dir>). ---
install(TARGETS main RUNTIME DESTINATION .)
install(FILES ${SHADER_OUTPUTS} DESTINATION .)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/config/" DESTINATION config)
if(WIN32)
    install(FILES
        "${MINGW_BIN}/libgcc_s_seh-1.dll" "${MINGW_BIN}/libstdc++-6.dll" "${MINGW_BIN}/libwinpthread-1.dll"
        "${SDL3_ROOT}/bin/SDL3.dll" "${VK_ROOT}/Bin/vulkan-1.dll"
        DESTINATION .)
endif()

# --- Example ---
if(REELHUNT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Tests (ctest) ---
if(REELHUNT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Dev tools ---
if(REELHUNT_BUILD_TOOLS)
    foreach(tool txd_dump ifp_dump col_dump bsp_dump inst_dump dff_dump pak_cat mls_dump grf_dump script_check exe_tables)
        add_executable(${tool} tools/${tool}.cpp)
        target_link_libraries(${tool} PRIVATE engine_core)
        copy_runtime_dlls(${tool})
    endforeach()
endif()
