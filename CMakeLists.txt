cmake_minimum_required(VERSION 3.16)
project(maniac_rebuild C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Native 64-bit toolchain per OS (Linux x86-64 is the verified target;
# Windows/macOS verification is deferred until those environments exist).
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# Asset directory baked in at compile time; overridable via MM_DATA_DIR
# env at runtime or -DMANIAC_DATA_DIR at configure time.
set(MANIAC_DATA_DIR "/home/wasd/MallManiacsUnmodified"
    CACHE PATH "Read-only game asset directory")

# Explicit source list (no glob) — game logic never calls SDL/GL directly;
# only platform_sdl2.c, gx_sdl2_gl.c, audio_sdl2.c plus the thin gx.c
# installer touch the new platform APIs.
set(MANIAC_LIB_SOURCES
    src/anim.c
    src/audio_sdl2.c
    src/charselect.c
    src/config.c
    src/custom_helpers.c
    src/font.c
    src/game.c
    src/gameplay.c
    src/gx.c
    src/gx_sdl2_gl.c
    src/hud.c
    src/input.c
    src/level.c
    src/level0.c
    src/level1.c
    src/level2.c
    src/level3.c
    src/level4.c
    src/levelselect.c
    src/menu.c
    src/mstring.c
    src/nav.c
    src/obj_collision.c
    src/obj_event.c
    src/obj_world.c
    src/options.c
    src/platform_sdl2.c
    src/player_ai.c
    src/player_camera.c
    src/player_physics.c
    src/player_setup.c
    src/pool.c
    src/quest.c
    src/record.c
    src/scene_alloc.c
    src/scene_render.c
    src/scene_system.c
    src/scene_text.c
    src/scene_transform.c
    src/sen.c
    src/sound.c
    src/stubs.c
    src/tga.c
    src/time.c
    src/util.c
    src/zone.c
)

add_library(maniac_lib STATIC ${MANIAC_LIB_SOURCES})
target_include_directories(maniac_lib PUBLIC src ${SDL2_INCLUDE_DIRS})
target_compile_definitions(maniac_lib PUBLIC
    MANIAC_DATA_DIR="${MANIAC_DATA_DIR}")
target_compile_options(maniac_lib PRIVATE
    -Wall -Wextra -Wpointer-to-int-cast)

add_executable(maniac_rebuild src/maniac.c)
target_link_libraries(maniac_rebuild PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)

# --- offline unit tests (no display/audio device needed) ---
enable_testing()

add_executable(test_foundation tests/test_foundation.c)
target_link_libraries(test_foundation PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME foundation COMMAND test_foundation)

add_executable(test_audio_offline tests/test_audio_offline.c)
target_link_libraries(test_audio_offline PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME audio_offline COMMAND test_audio_offline)

add_executable(test_gx_polygon tests/test_gx_polygon.c)
target_link_libraries(test_gx_polygon PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME gx_polygon COMMAND test_gx_polygon)

add_executable(test_sound_profile tests/test_sound_profile.c)
target_link_libraries(test_sound_profile PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME sound_profile COMMAND test_sound_profile)

add_executable(test_input tests/test_input.c)
target_link_libraries(test_input PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME input COMMAND test_input)

add_executable(test_quit_input tests/test_quit_input.c)
target_link_libraries(test_quit_input PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME quit_input COMMAND test_quit_input)

add_executable(test_menu_state tests/test_menu_state.c)
target_link_libraries(test_menu_state PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME menu_state COMMAND test_menu_state)

add_executable(test_levelselect_arrows tests/test_levelselect_arrows.c)
target_link_libraries(test_levelselect_arrows PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME levelselect_arrows COMMAND test_levelselect_arrows)

add_executable(test_pickup_transition tests/test_pickup_transition.c)
target_link_libraries(test_pickup_transition PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME pickup_transition COMMAND test_pickup_transition)

add_executable(test_event_item_positions tests/test_event_item_positions.c)
target_compile_definitions(test_event_item_positions PRIVATE
    MANIAC_DATA_DIR="${MANIAC_DATA_DIR}")
target_link_libraries(test_event_item_positions PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME event_item_positions COMMAND test_event_item_positions)

add_executable(test_gameplay_start tests/test_gameplay_start.c)
target_link_libraries(test_gameplay_start PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME gameplay_start COMMAND test_gameplay_start)

add_executable(test_record_dispatch tests/test_record_dispatch.c)
target_link_libraries(test_record_dispatch PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
add_test(NAME record_dispatch COMMAND test_record_dispatch)

# Offline font visualizer tool (no game launch needed).
add_executable(render_font tests/render_font.c)
target_link_libraries(render_font PRIVATE
    maniac_lib ${SDL2_LIBRARIES} OpenGL::GL m dl pthread)
