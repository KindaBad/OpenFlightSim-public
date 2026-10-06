include(FetchContent)
# Graphics dependencies never participate in OFS_BUILD_CLIENT=OFF.
# bgfx.cmake pins matching bgfx/bx/bimg submodule revisions. No moving branches.
set(OFS_DEPS_SOURCE_ROOT "" CACHE PATH "Optional pre-fetched dependency source directory")
set(ofs_pin_sdl 96292a5b464258a2b926e0a3d72f8b98c2a81aa6)
set(ofs_pin_bgfx_cmake de08a6080b39994ab8a9eddb82e79e18bc3df7bd)
set(ofs_pin_imgui f5befd2d29e66809cd1110a152e375a7f1981f06)
set(ofs_pin_glm 0af55ccecd98d4e5a8d1fad7de25ba429d60e863)
find_package(Git REQUIRED)
foreach(dep sdl bgfx_cmake imgui glm)
  if(OFS_DEPS_SOURCE_ROOT)
    if(NOT EXISTS "${OFS_DEPS_SOURCE_ROOT}/${dep}/.git")
      message(FATAL_ERROR "Missing pre-fetched ${dep} in OFS_DEPS_SOURCE_ROOT")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${OFS_DEPS_SOURCE_ROOT}/${dep}" rev-parse HEAD
      OUTPUT_VARIABLE actual_pin OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT actual_pin STREQUAL ofs_pin_${dep})
      message(FATAL_ERROR "${dep} source override does not match pinned revision ${ofs_pin_${dep}}")
    endif()
    string(TOUPPER "${dep}" dep_upper)
    set(FETCHCONTENT_SOURCE_DIR_${dep_upper} "${OFS_DEPS_SOURCE_ROOT}/${dep}")
  endif()
endforeach()
# Do not let an archive look up the parent project's unborn Git history.
set(SDL_REVISION "SDL3-3.2.20-${ofs_pin_sdl}" CACHE STRING "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLE_COMMON OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
# M0 uses X11/XWayland + OpenGL on Linux, Win32 + D3D11 on Windows.
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sdl
  URL https://codeload.github.com/libsdl-org/SDL/tar.gz/${ofs_pin_sdl}
  URL_HASH SHA256=4ae5eba49e7e346f742bea4c9966f4cc871face4b999513fcc1eb1f14d4dec30
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_Declare(bgfx_cmake GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
  GIT_TAG ${ofs_pin_bgfx_cmake}
  GIT_SUBMODULES bgfx bx bimg GIT_SUBMODULES_RECURSE TRUE)
FetchContent_Declare(imgui
  URL https://codeload.github.com/ocornut/imgui/tar.gz/${ofs_pin_imgui}
  URL_HASH SHA256=85f4ce357df05bcc331b587f01976f47fb55f19fadf477a7907289686bc3f4c8
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_Declare(glm
  URL https://codeload.github.com/g-truc/glm/tar.gz/${ofs_pin_glm}
  URL_HASH SHA256=e7f187d83523f505eb38dd25d297ea6c0d4ed856d733e808f18253f5a8fa88a0
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(sdl bgfx_cmake imgui glm)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${bgfx_cmake_SOURCE_DIR}" submodule status
  OUTPUT_VARIABLE ofs_submodules COMMAND_ERROR_IS_FATAL ANY)
if(ofs_submodules MATCHES "(^|\n)[-+U]")
  message(FATAL_ERROR "bgfx/bx/bimg submodules must match the pinned bgfx.cmake revision")
endif()
# Project shaders are compiled for OpenGL and, on Windows, D3D11.
if(WIN32)
  target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_DIRECT3D11=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_OPENGL=43)
else()
  message(FATAL_ERROR "M0 native client supports Linux and Windows")
endif()
set_property(DIRECTORY "${bgfx_cmake_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
# Treat upstream headers as external; project warnings apply to our targets.
get_target_property(bgfx_includes bgfx INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET bgfx PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${bgfx_includes}")
