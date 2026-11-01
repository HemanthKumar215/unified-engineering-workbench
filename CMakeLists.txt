cmake_minimum_required(VERSION 3.20)
project(UnifiedEngineeringWorkbench VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive-)
  set(WB_OPT_FLAGS "${CMAKE_CXX_FLAGS_RELEASE}")
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  set(WB_OPT_FLAGS "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

# Recorded in benchmark reports so timings can be reproduced.
set(WB_COMPILER_ID "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ---- Core library: all numerics, parsing, concurrency and device mocks ----
add_library(wb_core STATIC
  src/core/fea1d.cpp
  src/core/stl_parser.cpp
  src/core/nodal_csv.cpp
  src/core/batch.cpp
  src/core/sysinfo.cpp
  src/core/hardware.cpp
  src/core/backprop.cpp
  src/core/svg_plot.cpp
  src/core/json_writer.cpp
)
target_include_directories(wb_core PUBLIC src/core)
target_compile_definitions(wb_core PRIVATE
  WB_COMPILER="${WB_COMPILER_ID}"
  WB_OPT_FLAGS="${WB_OPT_FLAGS}")
find_package(Threads REQUIRED)
target_link_libraries(wb_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(wb_core PUBLIC psapi)
endif()

# ---- Launch.exe: local HTTP transport + static UI host ----
add_executable(Launch
  src/server/main_launch.cpp
  src/server/http_server.cpp
  src/server/api.cpp
  src/server/jobs.cpp
)
target_include_directories(Launch PRIVATE src/server)
target_link_libraries(Launch PRIVATE wb_core)
if(WIN32)
  target_link_libraries(Launch PRIVATE ws2_32 shell32)
  # GUI subsystem: no console window on double-click.
  set_target_properties(Launch PROPERTIES WIN32_EXECUTABLE ON)
endif()

# ---- Command-line tool for reproducible result generation ----
add_executable(wb_cli src/cli/wb_cli.cpp)
target_link_libraries(wb_cli PRIVATE wb_core)

# Static runtime so the packaged executables need no MinGW DLLs.
if(MINGW)
  foreach(t Launch wb_cli)
    target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
  endforeach()
endif()

# ---- Tests ----
enable_testing()
add_executable(wb_tests tests/test_main.cpp)
target_link_libraries(wb_tests PRIVATE wb_core)
if(MINGW)
  target_link_options(wb_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME wb_tests COMMAND wb_tests ${CMAKE_SOURCE_DIR}/tests/data)

# Diagnostic used in docs/report.md (scaling investigation); not part of the test suite.
add_executable(wb_diag_scaling tests/diag_scaling.cpp)
target_link_libraries(wb_diag_scaling PRIVATE wb_core)

# ---- Packaging: cmake --install build --prefix dist/Workbench ----
install(TARGETS Launch wb_cli RUNTIME DESTINATION .)
install(DIRECTORY src/frontend/ DESTINATION app)
install(DIRECTORY data/ DESTINATION data)
