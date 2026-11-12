cmake_minimum_required(VERSION 3.20)

project(heavenzip
  VERSION 0.1.0
  DESCRIPTION "CHCE orchestrator — chains prebuilt compression binaries into one pipeline"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HEAVENZIP_BUILD_TESTS "Build the test suite" ON)

add_library(heavenzip_core STATIC
  src/orchestrator/ini.cpp
  src/orchestrator/detector.cpp
  src/orchestrator/dispatcher.cpp)
target_include_directories(heavenzip_core PUBLIC include)
target_compile_definitions(heavenzip_core PUBLIC HEAVENZIP_VERSION="${PROJECT_VERSION}")

add_executable(heavenzip src/main.cpp)
target_link_libraries(heavenzip PRIVATE heavenzip_core)

if(MSVC)
  target_compile_options(heavenzip_core PRIVATE /W4)
else()
  target_compile_options(heavenzip_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(HEAVENZIP_BUILD_TESTS)
  enable_testing()
  add_executable(unit_ini tests/unit/test_ini.cpp)
  target_link_libraries(unit_ini PRIVATE heavenzip_core)
  add_test(NAME unit.ini COMMAND unit_ini "${CMAKE_CURRENT_SOURCE_DIR}/config")

  add_executable(unit_detector tests/unit/test_detector.cpp)
  target_link_libraries(unit_detector PRIVATE heavenzip_core)
  add_test(NAME unit.detector COMMAND unit_detector "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

  add_executable(unit_dispatcher tests/unit/test_dispatcher.cpp)
  target_link_libraries(unit_dispatcher PRIVATE heavenzip_core)
  add_test(NAME unit.dispatcher COMMAND unit_dispatcher "${CMAKE_CURRENT_SOURCE_DIR}/config"
                                                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

  # The phase-0 exit test, run as a test so it cannot rot: `heavenzip validate`
  # must succeed against the config this repository ships.
  add_test(NAME cli.validate COMMAND heavenzip validate --config "${CMAKE_CURRENT_SOURCE_DIR}/config")
  add_test(NAME cli.version COMMAND heavenzip version)
endif()
