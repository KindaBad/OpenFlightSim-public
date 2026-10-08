find_package(Python3 COMPONENTS Interpreter REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/scripts/generate_physical_configs.py"
  "${CMAKE_SOURCE_DIR}/data/physics/a320.json" "${CMAKE_SOURCE_DIR}/data/physics/su57.json")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/generate_physical_configs.py"
  --output "${CMAKE_CURRENT_BINARY_DIR}/physical_configs.cpp" COMMAND_ERROR_IS_FATAL ANY)
add_library(ofs_core STATIC
  src/atmosphere.cpp
  src/weapons.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/physical_configs.cpp"
  src/airliner.cpp
  src/unsteady_aero.cpp
  src/geometry_debug.cpp
  src/aircraft.cpp
  src/aircraft_definition.cpp
  src/sr71.cpp
  src/su57.cpp
  src/jf17.cpp
  src/control_allocation.cpp
  src/data_model.cpp
  src/dynamics.cpp
  src/environment_models.cpp
  src/aerodynamic_model.cpp
  src/propulsion_model.cpp
  src/mass_properties.cpp
  src/dynamics_evaluation.cpp
  src/flight_control_system.cpp
  src/simulator.cpp
  src/damage.cpp
  src/ground_service.cpp
  src/water.cpp
  src/trim.cpp
  src/c_api.cpp
)
target_include_directories(ofs_core PUBLIC include)
target_compile_definitions(ofs_core PRIVATE OFS_INCLUDE_SU57=$<BOOL:${OFS_INCLUDE_SU57}>
  OFS_INCLUDE_JF17=$<BOOL:${OFS_INCLUDE_JF17}>)
ofs_target_defaults(ofs_core)

option(OFS_ENABLE_F16_REFERENCE "Build NASA F-16 reference model from locally imported data" OFF)
set(OFS_F16_DATA_DIR "${CMAKE_SOURCE_DIR}/.cache/reference/f16" CACHE PATH "Output of import_nasa_f16.py")
if(OFS_ENABLE_F16_REFERENCE)
  if(NOT EXISTS "${OFS_F16_DATA_DIR}/f16_imported.hpp")
    message(FATAL_ERROR "Import NASA data first: python3 scripts/import_nasa_f16.py")
  endif()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/import_nasa_f16.py"
    --verify --output "${OFS_F16_DATA_DIR}" RESULT_VARIABLE reference_integrity)
  if(NOT reference_integrity EQUAL 0)
    message(FATAL_ERROR "Reference import integrity failed; rerun the pinned NASA importer")
  endif()
  target_sources(ofs_core PRIVATE src/f16_reference.cpp)
  target_include_directories(ofs_core PRIVATE "${OFS_F16_DATA_DIR}")
  target_compile_definitions(ofs_core PUBLIC OFS_ENABLE_F16_REFERENCE=1)
endif()
