cmake_minimum_required(VERSION 3.24)
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(environment_loader "${project_root}/cmake/HtmsrEnvironment.cmake")
file(TO_CMAKE_PATH "$ENV{TEMP}" temporary_root)
if(temporary_root STREQUAL "")
    set(temporary_root "${project_root}/out")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef test_id)
set(test_root "${temporary_root}/htmsr-build-config-${test_id}")
file(MAKE_DIRECTORY "${test_root}/config" "${test_root}/deps/PCL with spaces/cmake"
    "${test_root}/deps/PCL with spaces/3rdParty/Boost/lib/cmake/Boost-9.8.7"
    "${test_root}/deps/PCL with spaces/3rdParty/Qhull/lib/cmake/Qhull")
set(HTMSR_ENVIRONMENT_FILE "${test_root}/config/environment.ini")
set(CMAKE_BUILD_TYPE Release)
set(Qt5_DIR "old-computer/Qt" CACHE PATH "Stale preset" FORCE)
set(Boost_DIR "old-computer/Boost" CACHE PATH "Stale package result" FORCE)
set(CMAKE_PREFIX_PATH "old-computer/prefix" CACHE STRING "Stale preset" FORCE)
set(CMAKE_CXX_COMPILER "compiler-must-be-retained" CACHE STRING "Toolchain sentinel" FORCE)
file(WRITE "${HTMSR_ENVIRONMENT_FILE}" [=[
; 中文注释；测试只修改临时配置。
[build]
Qt5_DIR=../deps/Qt with spaces/lib/cmake/Qt5
OpenCV_DIR=../deps/OpenCV/lib
PCL_DIR=../deps/PCL with spaces/cmake
VTK_DIR_DEBUG=../deps/VTK-debug/cmake
VTK_DIR=../deps/VTK-release/cmake
HTMSR_EIGEN_INCLUDE_DIR=../deps/Eigen
CMAKE_PREFIX_PATH=
HTMSR_ENABLE_VTK_VIEWER_DEBUG=false
HTMSR_ENABLE_VTK_VIEWER=true
[runtime]
extraDllDirectories=runtime-is-not-a-build-setting
]=])
include("${environment_loader}")
if(NOT Qt5_DIR STREQUAL "${test_root}/deps/Qt with spaces/lib/cmake/Qt5")
    message(FATAL_ERROR "Config did not override the preset or resolve a relative path with spaces: ${Qt5_DIR}")
endif()
foreach(expected_prefix
    "${test_root}/deps/PCL with spaces"
    "${test_root}/deps/PCL with spaces/3rdParty/Boost/lib/cmake/Boost-9.8.7"
    "${test_root}/deps/PCL with spaces/3rdParty/Qhull/lib/cmake/Qhull"
    "${test_root}/deps/VTK-release/cmake")
    if(NOT expected_prefix IN_LIST CMAKE_PREFIX_PATH)
        message(FATAL_ERROR "Missing automatically derived prefix: ${expected_prefix}")
    endif()
endforeach()
if(DEFINED Boost_DIR OR CMAKE_PREFIX_PATH MATCHES "old-computer" OR DEFINED extraDllDirectories)
    message(FATAL_ERROR "Stale package cache/preset or runtime section leaked into build settings")
endif()
if(NOT CMAKE_CXX_COMPILER STREQUAL "compiler-must-be-retained")
    message(FATAL_ERROR "Dependency refresh altered the compiler toolchain")
endif()
set(release_fingerprint "${HTMSR_BUILD_ENVIRONMENT_FINGERPRINT}")
set(Boost_DIR "keep-unchanged-environment" CACHE PATH "Package result" FORCE)
include("${environment_loader}")
if(NOT Boost_DIR STREQUAL "keep-unchanged-environment")
    message(FATAL_ERROR "Unchanged configuration needlessly cleared package cache")
endif()

set(CMAKE_BUILD_TYPE Debug)
include("${environment_loader}")
if(HTMSR_ENABLE_VTK_VIEWER OR NOT VTK_DIR STREQUAL "${test_root}/deps/VTK-debug/cmake")
    message(FATAL_ERROR "Debug overrides depend on INI key order")
endif()
if(DEFINED Boost_DIR OR HTMSR_BUILD_ENVIRONMENT_FINGERPRINT STREQUAL release_fingerprint)
    message(FATAL_ERROR "Build configuration change did not refresh dependency cache")
endif()

# 换依赖目录后，只改 config：旧 Boost 查找结果失效，额外前缀和空 Debug 覆盖生效。
file(WRITE "${HTMSR_ENVIRONMENT_FILE}" [=[
[build]
Qt5_DIR=../new-deps/Qt/lib/cmake/Qt5
OpenCV_DIR=../new-deps/OpenCV/lib
PCL_DIR=../new-deps/PCL/cmake
VTK_DIR=../new-deps/VTK/cmake
VTK_DIR_DEBUG=
CMAKE_PREFIX_PATH=../extra one;../extra two
HTMSR_ENABLE_VTK_VIEWER=false
]=])
set(Boost_DIR "old-computer/Boost" CACHE PATH "Stale result" FORCE)
include("${environment_loader}")
foreach(expected_prefix "${test_root}/extra one" "${test_root}/extra two"
    "${test_root}/new-deps/Qt/lib/cmake/Qt5" "${test_root}/new-deps/VTK/cmake")
    if(NOT expected_prefix IN_LIST CMAKE_PREFIX_PATH)
        message(FATAL_ERROR "Config-only path change or semicolon list failed: ${expected_prefix}")
    endif()
endforeach()
if(DEFINED Boost_DIR OR CMAKE_PREFIX_PATH MATCHES "deps/PCL with spaces" OR CMAKE_PREFIX_PATH MATCHES "VTK-debug")
    message(FATAL_ERROR "Removed dependency paths survived in cache/search prefixes")
endif()

# 损坏配置应当失败并保留原文，不能悄悄沿用预设或缓存。
set(child_script "${test_root}/invalid-config.cmake")
file(WRITE "${child_script}" "cmake_minimum_required(VERSION 3.24)\nset(HTMSR_ENVIRONMENT_FILE \"${HTMSR_ENVIRONMENT_FILE}\")\ninclude(\"${environment_loader}\")\n")
foreach(invalid_case "[build]\nQt5_DIR=a\nQt5_DIR=b\n" "[build]\nthis is not a parameter\n")
    file(WRITE "${HTMSR_ENVIRONMENT_FILE}" "${invalid_case}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -P "${child_script}"
        RESULT_VARIABLE child_result OUTPUT_VARIABLE child_output ERROR_VARIABLE child_error)
    file(READ "${HTMSR_ENVIRONMENT_FILE}" after_failure)
    if(child_result EQUAL 0 OR NOT after_failure STREQUAL invalid_case)
        message(FATAL_ERROR "Invalid build configuration was accepted or overwritten: ${child_output}${child_error}")
    endif()
endforeach()
cmake_path(IS_PREFIX temporary_root "${test_root}" NORMALIZE inside_temporary_root)
get_filename_component(test_directory_name "${test_root}" NAME)
if(NOT inside_temporary_root OR NOT test_directory_name MATCHES "^htmsr-build-config-[0-9a-f]+$")
    message(FATAL_ERROR "Refusing to remove unexpected temporary path: ${test_root}")
endif()
file(REMOVE_RECURSE "${test_root}")
message(STATUS "PASS: config-only paths, automatic prefixes, cache refresh, Debug overrides and invalid-file protection")
