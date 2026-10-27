# tests/CMakeLists.txt - engine test executables, registered with ctest.
#
#   ctest --test-dir <build> --output-on-failure
#
# Tests that need game data or reference fixtures read their locations from test_paths.h
# (defaults below, overridable with REELHUNT_GAME_ROOT / REELHUNT_TEST_FIXTURES / REELHUNT_SAVE_DIR)
# and exit with 77 ("skipped") when an optional input such as the original save is missing.

if(DEFINED ENV{TEMP})
    file(TO_CMAKE_PATH "$ENV{TEMP}/reelhunt_fixtures" _default_fixtures)
else()
    set(_default_fixtures "/tmp/reelhunt_fixtures")
endif()
set(REELHUNT_TEST_FIXTURES "${_default_fixtures}" CACHE PATH
    "Reference fixtures (FFmpeg decodes) for bink_test / bink_audio_test")

function(engine_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE engine_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE
        REELHUNT_TEST_GAME_ROOT="${REELHUNT_GAME_ROOT}"
        REELHUNT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        REELHUNT_TEST_FIXTURES_DIR="${REELHUNT_TEST_FIXTURES}")
    copy_runtime_dlls(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()

# Pure unit tests for core types, math, physics, parsers and game logic (no game data needed).
engine_test(core_test core_test.cpp)

# Parsers against the shipped game data (DFF/TXD/IFP/INST/COL/BSP/MLS/SFX).
engine_test(assets_test assets_test.cpp)

# librw parse-only smoke test.
engine_test(librw_test librw_test.cpp)

# Bink video decode bit-exactness vs an FFmpeg reference.
engine_test(bink_test bink_test.cpp)

# Bink audio decode vs an FFmpeg reference (Pearson correlation).
engine_test(bink_audio_test bink_audio_test.cpp)

# Save system: parse the original save slot 1, typed restore, write-back round trip.
engine_test(save_test save_test.cpp)

# G1 player controller movement + follow-camera math.
engine_test(player_test player_test.cpp)

# Frontend: game text, fonts, scene list and main-menu navigation against the shipped data.
engine_test(frontend_test frontend_test.cpp)

# G1.5 key-mapping file + every bound clip exists in the IFP and drives Cash's skeleton.
engine_test(anim_test anim_test.cpp)

# Level-script compiler + engine: language semantics, every shipped script compiles.
engine_test(script_test script_test.cpp)

# Hunter AI: navigation graph, senses, the level script's AI set-up (Jury_Turf).
engine_test(ai_test ai_test.cpp)
