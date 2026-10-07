add_library(ofs_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
target_include_directories(ofs_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(ofs_imgui PUBLIC SDL3::SDL3)

# ---------------------------------------------------------------------------
# Shaders
# ---------------------------------------------------------------------------
# Compile bgfx's portable shader language with the matching pinned shaderc.
set(shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(shader_header "${CMAKE_CURRENT_BINARY_DIR}/generated/ofs_shaders.hpp")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(shader_sources)
foreach(stage vs fs)
  file(GLOB found "${shader_dir}/*_${stage}.glsl")
  list(APPEND shader_sources ${found})
endforeach()
list(LENGTH shader_sources shader_count)
set(shader_backends "glsl")
if(WIN32)
  set(shader_backends "glsl,dx11")
endif()
if(shader_count EQUAL 0)
  message(FATAL_ERROR "No GLSL shaders found in ${shader_dir}")
endif()
add_custom_command(
  OUTPUT "${shader_header}"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/pack_shaders.py"
          "${shader_header}" "${shader_dir}" "$<TARGET_FILE:shaderc>" "${BGFX_DIR}" "${shader_backends}"
  DEPENDS shaderc ${shader_sources} "${shader_dir}/common.glsl" "${shader_dir}/varying.def.sc"
          "${CMAKE_SOURCE_DIR}/scripts/pack_shaders.py"
  COMMENT "Compiling backend-specific bgfx shader binaries"
  VERBATIM)
add_custom_target(ofs_shaders DEPENDS "${shader_header}")
foreach(stage vs_ocornut_imgui fs_ocornut_imgui)
  configure_file("${BGFX_DIR}/examples/common/imgui/${stage}.bin.h"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/${stage}.bin.h" COPYONLY)
endforeach()

# Embed the OFL-licensed Karla font supplied by the pinned ImGui dependency.
# A 30 px atlas keeps the larger flight readouts sharp without runtime files.
file(READ "${imgui_SOURCE_DIR}/misc/fonts/Karla-Regular.ttf" ui_font_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," ui_font_bytes "${ui_font_hex}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/ofs_font.hpp"
  "#pragma once\ninline const unsigned char ofs_ui_font[] = {${ui_font_bytes}};\n")

add_executable(ofs_client
  src/main.cpp src/platform.cpp src/input.cpp src/renderer.cpp
  src/debug_ui.cpp src/hud.cpp src/camera.cpp src/settings.cpp)
add_dependencies(ofs_client ofs_shaders)
target_include_directories(ofs_client PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(ofs_client PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(ofs_client PRIVATE ofs_visual_core SDL3::SDL3 bgfx ofs_imgui glm::glm)
target_compile_definitions(ofs_client PRIVATE OFS_BUILD_TYPE="$<CONFIG>" SDL_MAIN_HANDLED)
target_compile_definitions(ofs_client PRIVATE OFS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
install(TARGETS ofs_client RUNTIME_DEPENDENCY_SET ofs_runtime RUNTIME DESTINATION .)
install(FILES "${CMAKE_SOURCE_DIR}/output/Airbus_A320.glb" DESTINATION output)
install(FILES "${CMAKE_SOURCE_DIR}/assets/aircraft/typhoon/typhoon_lod0.glb"
  "${CMAKE_SOURCE_DIR}/assets/aircraft/typhoon/typhoon_lod1.glb"
  "${CMAKE_SOURCE_DIR}/assets/aircraft/typhoon/typhoon_lod2.glb"
  "${CMAKE_SOURCE_DIR}/assets/aircraft/typhoon/typhoon_lod3.glb" DESTINATION assets/aircraft/typhoon)
install(FILES "${CMAKE_SOURCE_DIR}/assets/aircraft/sr71/sr71_lod0.glb"
  "${CMAKE_SOURCE_DIR}/assets/aircraft/sr71/sr71_lod1.glb"
  "${CMAKE_SOURCE_DIR}/assets/aircraft/sr71/sr71_lod2.glb"
  "${CMAKE_SOURCE_DIR}/assets/aircraft/sr71/sr71_lod3.glb" DESTINATION assets/aircraft/sr71)
if(OFS_INCLUDE_SU57)
  if(OFS_PROTECTED_SU57_ROOT)
    foreach(lod RANGE 0 3)
      install(FILES "${OFS_PROTECTED_SU57_ROOT}/assets/aircraft/su57/su57_lod${lod}.glb.ofspack"
        DESTINATION assets/aircraft/su57)
    endforeach()
  else()
    install(FILES "${CMAKE_SOURCE_DIR}/assets/aircraft/su57/su57_lod0.glb"
      "${CMAKE_SOURCE_DIR}/assets/aircraft/su57/su57_lod1.glb"
      "${CMAKE_SOURCE_DIR}/assets/aircraft/su57/su57_lod2.glb"
      "${CMAKE_SOURCE_DIR}/assets/aircraft/su57/su57_lod3.glb"
      "${CMAKE_SOURCE_DIR}/assets/aircraft/su57/README.md" DESTINATION assets/aircraft/su57)
  endif()
endif()
if(OFS_BUILD_NETWORK)
  target_link_libraries(ofs_client PRIVATE ofs_net)
  target_compile_definitions(ofs_client PRIVATE OFS_NETWORK_ENABLED=1)
endif()
ofs_target_defaults(ofs_client)

# Asset-agnostic client logic (glTF loader, mesh builder, camera, effects,
# settings) is linked into a headless test binary so it can be exercised
# without a graphics context.
add_library(ofs_client_core STATIC src/camera.cpp src/settings.cpp)
target_include_directories(ofs_client_core PUBLIC src)
target_link_libraries(ofs_client_core PUBLIC ofs_visual_core glm::glm)
ofs_target_defaults(ofs_client_core)

if(OFS_BUILD_TESTS)
  add_executable(ofs_input_tests ../tests/client_input_tests.cpp src/input.cpp)
  target_link_libraries(ofs_input_tests PRIVATE ofs_client_core SDL3::SDL3 ofs_imgui)
  ofs_target_defaults(ofs_input_tests)
  add_test(NAME client.flight_input COMMAND ofs_input_tests)
  add_executable(ofs_client_math_tests ../tests/client_math_tests.cpp)
  target_link_libraries(ofs_client_math_tests PRIVATE ofs_client_core)
  ofs_target_defaults(ofs_client_math_tests)
  add_test(NAME client.coordinates_interpolation COMMAND ofs_client_math_tests)

  # The asset suite is a separate binary: it loads the delivered A320, which is
  # a large read, and reports its own measurements.
  add_executable(ofs_client_asset_tests ../tests/client_asset_tests.cpp)
  target_link_libraries(ofs_client_asset_tests PRIVATE ofs_client_core)
  ofs_target_defaults(ofs_client_asset_tests)
  add_test(NAME client.asset_pipeline
    COMMAND ofs_client_asset_tests)
  if(OFS_CLIENT_SMOKE_TEST)
    add_test(NAME client.graphical_smoke
      COMMAND ofs_client --smoke-test --screenshot "${CMAKE_CURRENT_BINARY_DIR}/smoke.ppm")
    set_tests_properties(client.graphical_smoke PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    if(OFS_BUILD_NETWORK)
      add_test(NAME client.dogfight_smoke
        COMMAND ofs_client --dogfight-smoke --screenshot "${CMAKE_CURRENT_BINARY_DIR}/dogfight.ppm")
      set_tests_properties(client.dogfight_smoke PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "graphical;combat")
    endif()
  endif()
endif()

if(OFS_BUILD_TESTS)
  add_executable(ofs_shader_conformance ../tests/shader_conformance.cpp src/platform.cpp)
  add_dependencies(ofs_shader_conformance ofs_shaders)
  target_include_directories(ofs_shader_conformance PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(ofs_shader_conformance PRIVATE SDL3::SDL3 bgfx)
  target_compile_definitions(ofs_shader_conformance PRIVATE SDL_MAIN_HANDLED)
  ofs_target_defaults(ofs_shader_conformance)
  if(OFS_CLIENT_SMOKE_TEST)
    add_test(NAME client.shader_conformance COMMAND ofs_shader_conformance)
    set_tests_properties(client.shader_conformance PROPERTIES RUN_SERIAL TRUE LABELS "regression-plausibility;graphical" TIMEOUT 60)
  endif()
endif()
