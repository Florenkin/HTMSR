cmake_minimum_required(VERSION 3.25)
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef test_id)
set(test_root "${project_root}/out/header-dependency-${test_id}")
file(MAKE_DIRECTORY "${test_root}/source")
file(WRITE "${test_root}/source/layout.h" "#define LAYOUT_VALUE 1\n")
file(WRITE "${test_root}/source/reader.cpp" "#include \"layout.h\"\nint value() { return LAYOUT_VALUE; }\n")
set(fixture [=[
cmake_minimum_required(VERSION 3.25)
project(HeaderDependencyProbe LANGUAGES CXX)
@dependency_settings@
add_library(probe OBJECT reader.cpp)
]=])

# 负例故意模拟损坏的本地化前缀，确认测试确实能识别“构建成功但漏记依赖”。
set(dependency_settings "set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX \"invalid-prefix: \")\nset(CMAKE_CL_SHOWINCLUDES_PREFIX \"invalid-prefix: \")")
string(CONFIGURE "${fixture}" broken_fixture @ONLY)
file(WRITE "${test_root}/source/CMakeLists.txt" "${broken_fixture}")
function(configure_and_build directory)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env VSLANG=2052 --
        "${CMAKE_COMMAND}" -S "${test_root}/source" -B "${directory}" -G Ninja
        "-DCMAKE_CXX_COMPILER=${HTMSR_TEST_COMPILER}"
        "-DCMAKE_MAKE_PROGRAM=${HTMSR_TEST_NINJA}"
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Dependency fixture configure failed: ${output}${error}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env VSLANG=2052 --
        "${CMAKE_COMMAND}" --build "${directory}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Dependency fixture build failed: ${output}${error}")
    endif()
endfunction()
configure_and_build("${test_root}/broken")
execute_process(COMMAND "${HTMSR_TEST_NINJA}" -C "${test_root}/broken" -t deps
    OUTPUT_VARIABLE broken_dependencies RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR broken_dependencies MATCHES "[/\\]layout\\.h")
    message(FATAL_ERROR "Broken-prefix control must omit the header dependency: ${broken_dependencies}")
endif()

set(dependency_settings "include(\"${project_root}/cmake/HtmsrMsvcDependencies.cmake\")")
string(CONFIGURE "${fixture}" fixed_fixture @ONLY)
file(WRITE "${test_root}/source/CMakeLists.txt" "${fixed_fixture}")
configure_and_build("${test_root}/fixed")
execute_process(COMMAND "${HTMSR_TEST_NINJA}" -C "${test_root}/fixed" -t deps
    OUTPUT_VARIABLE dependencies RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT dependencies MATCHES "[/\\]layout\\.h")
    message(FATAL_ERROR "Fixed compiler invocation must record layout.h: ${dependencies}")
endif()
set(object "${test_root}/fixed/CMakeFiles/probe.dir/reader.cpp.obj")
file(SHA256 "${object}" before_change)
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${test_root}/source/layout.h" "#define LAYOUT_VALUE 2\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env VSLANG=2052 --
    "${CMAKE_COMMAND}" --build "${test_root}/fixed"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(SHA256 "${object}" after_change)
if(NOT result EQUAL 0 OR before_change STREQUAL after_change)
    message(FATAL_ERROR "Changing only the header must recompile reader.cpp: ${output}${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${test_root}/fixed"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "no work to do")
    message(FATAL_ERROR "Unchanged fixture must not rebuild unnecessarily: ${output}${error}")
endif()

set(output_root "${project_root}/out")
cmake_path(IS_PREFIX output_root "${test_root}" NORMALIZE inside_output_root)
get_filename_component(test_name "${test_root}" NAME)
if(NOT inside_output_root OR NOT test_name MATCHES "^header-dependency-[0-9a-f]+$")
    message(FATAL_ERROR "Refusing to clean unexpected fixture: ${test_root}")
endif()
file(REMOVE_RECURSE "${test_root}")
message(STATUS "PASS: broken-prefix control, recorded header, header-only rebuild and unchanged no-op")
