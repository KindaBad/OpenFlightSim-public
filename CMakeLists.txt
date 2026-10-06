cmake_minimum_required(VERSION 3.24)
project(OpenFlightSim VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Installed Linux executables look in their own lib/ folder first, so a release
# carries the protobuf runtime instead of depending on the player's distribution.
if(UNIX AND NOT APPLE)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
endif()
option(OFS_BUILD_CLIENT "Build SDL3/bgfx/ImGui native client" ON)
option(OFS_BUILD_NETWORK "Build GameNetworkingSockets server/client foundation" ON)
option(OFS_BUILD_TESTS "Build headless CTest suite" ON)
option(OFS_CLIENT_SMOKE_TEST "Register graphical smoke test (needs a desktop)" OFF)
set(OFS_PRODUCTION_ASSET_ROOT "${CMAKE_SOURCE_DIR}" CACHE PATH "Root of installed production asset pack used by content tests")
option(OFS_REQUIRE_PRODUCTION_ASSETS "Fail asset validation if any registry model/LOD is absent" OFF)
option(OFS_SANITIZE "Instrument project targets with ASan/UBSan (GCC/Clang)" OFF)
if(OFS_BUILD_GDEXT)
  message(FATAL_ERROR "The unimplemented Godot option is retired. Use OFS_BUILD_CLIENT.")
endif()

function(ofs_target_defaults target)
  target_compile_features(${target} PUBLIC cxx_std_23)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(OFS_SANITIZE)
    if(MSVC)
      message(FATAL_ERROR "OFS_SANITIZE currently requires GCC or Clang")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PUBLIC -fsanitize=address,undefined)
  endif()
endfunction()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_subdirectory(core)
include(cmake/Version.cmake)
add_executable(ofs_catalog launcher/catalog.cpp)
target_link_libraries(ofs_catalog PRIVATE ofs_core)
target_include_directories(ofs_catalog PRIVATE "${CMAKE_BINARY_DIR}/generated")
ofs_target_defaults(ofs_catalog)
if(OFS_BUILD_NETWORK)
  target_compile_definitions(ofs_catalog PRIVATE OFS_NETWORK_ENABLED=1)
endif()
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/launcher-catalog.json"
  COMMAND ofs_catalog "${CMAKE_BINARY_DIR}/launcher-catalog.json"
  DEPENDS ofs_catalog VERBATIM)
add_custom_target(ofs_launcher_catalog ALL DEPENDS "${CMAKE_BINARY_DIR}/launcher-catalog.json")
install(FILES "${CMAKE_BINARY_DIR}/launcher-catalog.json" "${CMAKE_BINARY_DIR}/build-info.json" DESTINATION .)
install(DIRECTORY data/launcher/ DESTINATION data/launcher)
# CPU asset/animation/effect logic has no window, graphics API or GLM dependency.
if(OFS_BUILD_CLIENT OR OFS_BUILD_TESTS)
  add_library(ofs_visual_core STATIC client/src/gltf.cpp client/src/json.cpp
    client/src/mesh.cpp client/src/effects.cpp client/src/animation.cpp client/src/texture.cpp)
  target_include_directories(ofs_visual_core PUBLIC client/src)
  target_include_directories(ofs_visual_core SYSTEM PRIVATE client/thirdparty)
  target_link_libraries(ofs_visual_core PUBLIC ofs_core)
  ofs_target_defaults(ofs_visual_core)
endif()
if(OFS_BUILD_NETWORK)
  add_subdirectory(network)
endif()
if(OFS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_test(NAME launcher.security_config COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s launcher/tests -v)
  set_tests_properties(launcher.security_config PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 90)
endif()
if(OFS_BUILD_CLIENT)
  include(cmake/Dependencies.cmake)
  add_subdirectory(client)
endif()
if(WIN32 AND (OFS_BUILD_CLIENT OR OFS_BUILD_NETWORK))
  install(RUNTIME_DEPENDENCY_SET ofs_runtime
    DIRECTORIES "${CMAKE_BINARY_DIR}/client/Release" "${CMAKE_BINARY_DIR}/network/Release"
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*"
    RUNTIME DESTINATION .)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
  include(InstallRequiredSystemLibraries)
endif()
if(UNIX AND NOT APPLE AND (OFS_BUILD_CLIENT OR OFS_BUILD_NETWORK))
  # protobuf's soname differs between distributions (Fedora, Ubuntu, Arch all
  # disagree). Graphics, OpenSSL 3 and C/C++ runtimes stay with the system.
  install(RUNTIME_DEPENDENCY_SET ofs_runtime
    POST_INCLUDE_REGEXES "libprotobuf[^/]*$" "libabsl[^/]*$" "libutf8_(range|validity)[^/]*$"
    POST_EXCLUDE_REGEXES ".*"
    LIBRARY DESTINATION lib)
endif()
