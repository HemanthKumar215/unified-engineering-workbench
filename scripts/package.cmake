# Packaging only (no backend features): assembles the submission folder and ZIP.
#
#   cmake -P scripts/package.cmake                      (CandidateID defaults to "CandidateID")
#   cmake -DCANDIDATE_ID=RA2211003010xxx -P scripts/package.cmake
#
# Expects a Release build in build/ (see README, "Build from source").
cmake_minimum_required(VERSION 3.20)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED CANDIDATE_ID)
  set(CANDIDATE_ID "CandidateID")
endif()
set(NAME "${CANDIDATE_ID}_Workbench")
set(STAGE "${ROOT}/dist/${NAME}")
set(BUILD "${ROOT}/build")

foreach(exe Launch.exe wb_cli.exe wb_tests.exe)
  if(NOT EXISTS "${BUILD}/${exe}")
    message(FATAL_ERROR "${BUILD}/${exe} not found - build the project first.")
  endif()
endforeach()

file(REMOVE_RECURSE "${STAGE}")
file(MAKE_DIRECTORY "${STAGE}")

# Executables at the root; the UI goes to app/ where Launch.exe looks for it.
file(COPY "${BUILD}/Launch.exe" "${BUILD}/wb_cli.exe" "${BUILD}/wb_tests.exe" DESTINATION "${STAGE}")
file(COPY "${ROOT}/src/frontend/" DESTINATION "${STAGE}/app")

# Sources, data, evidence and docs (relative layout preserved).
foreach(dir src tests data results docs .vscode scripts)
  if(EXISTS "${ROOT}/${dir}")
    file(COPY "${ROOT}/${dir}" DESTINATION "${STAGE}")
  endif()
endforeach()
foreach(f CMakeLists.txt README.md .gitignore .gitattributes)
  if(EXISTS "${ROOT}/${f}")
    file(COPY "${ROOT}/${f}" DESTINATION "${STAGE}")
  endif()
endforeach()

# Version-control evidence: a full Git bundle plus a readable log.
file(MAKE_DIRECTORY "${STAGE}/vcs")
execute_process(COMMAND git -C "${ROOT}" bundle create "${STAGE}/vcs/workbench.bundle" --all RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "git bundle failed")
endif()
execute_process(COMMAND git -C "${ROOT}" log --stat --date=iso
                OUTPUT_FILE "${STAGE}/vcs/commit_log.txt")
execute_process(COMMAND git -C "${ROOT}" log --oneline --date=short --format=%h\ %ad\ %s
                OUTPUT_FILE "${STAGE}/vcs/commit_log_oneline.txt")

# Dependency/version record captured from the toolchain that built this package.
execute_process(COMMAND "${CMAKE_COMMAND}" --version OUTPUT_VARIABLE cmake_ver)
file(READ "${BUILD}/CMakeCache.txt" cache)
string(REGEX MATCH "CMAKE_CXX_COMPILER:[A-Z]+=([^\n]*)" _ "${cache}")
set(cxx "${CMAKE_MATCH_1}")
execute_process(COMMAND "${cxx}" --version OUTPUT_VARIABLE cxx_ver)
string(REGEX REPLACE "\n.*" "" cxx_ver "${cxx_ver}")
string(REGEX REPLACE "\n.*" "" cmake_ver "${cmake_ver}")
file(WRITE "${STAGE}/docs/build_versions.txt"
  "Built ${CANDIDATE_ID} package\n"
  "Compiler: ${cxx_ver}\n"
  "${cmake_ver}\n"
  "Generator: Ninja; build type Release (-O3 -DNDEBUG); static libgcc/libstdc++/winpthread\n"
  "Third-party libraries: none (C++ standard library and Win32 API only)\n")

file(REMOVE "${ROOT}/dist/${NAME}.zip")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${ROOT}/dist/${NAME}.zip" --format=zip "${NAME}"
                WORKING_DIRECTORY "${ROOT}/dist" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "zip failed")
endif()
message(STATUS "Wrote dist/${NAME}.zip")
