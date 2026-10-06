# One application version source: project(VERSION) in the root CMakeLists.txt.
find_package(Git QUIET)
set(OFS_COMMIT "unknown")
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE OFS_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
string(TIMESTAMP OFS_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
set(OFS_RELEASE_CHANNEL "stable" CACHE STRING "stable or development")
set(OFS_MIN_LAUNCHER_VERSION "0.3.0" CACHE STRING "Oldest launcher compatible with this release format")
if(NOT OFS_RELEASE_CHANNEL MATCHES "^(stable|development)$")
  message(FATAL_ERROR "OFS_RELEASE_CHANNEL must be stable or development")
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file("${CMAKE_SOURCE_DIR}/cmake/ofs_version.hpp.in" "${CMAKE_BINARY_DIR}/generated/ofs_version.hpp" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/cmake/build-info.json.in" "${CMAKE_BINARY_DIR}/build-info.json" @ONLY)
