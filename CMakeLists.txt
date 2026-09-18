cmake_minimum_required(VERSION 3.24)

# 分类配置中的依赖路径是唯一来源；构建预设只指定构建类型和输出目录。
include(cmake/HtmsrEnvironment.cmake)

project(HTMSR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HTMSR_ENABLE_VTK_VIEWER "Build the embedded PCL/VTK point cloud viewer" ON)
option(HTMSR_REQUIRE_VTK_VIEWER "Fail configuration if the embedded VTK Qt point cloud viewer cannot be enabled" OFF)
option(HTMSR_ENABLE_HIK_CAMERA "Build Hikrobot MVS camera acquisition support" OFF)

function(htmsr_normalize_compile_definitions output_variable)
    set(normalized_definitions)
    foreach(definition IN LISTS ARGN)
        if(NOT definition)
            continue()
        endif()

        string(REPLACE " " ";" definition_parts "${definition}")
        foreach(definition_part IN LISTS definition_parts)
            if(NOT definition_part)
                continue()
            endif()

            if(definition_part MATCHES "^\\$<")
                list(APPEND normalized_definitions "${definition_part}")
                continue()
            endif()

            string(REGEX REPLACE "^-D" "" definition_part "${definition_part}")
            if(definition_part MATCHES "-D")
                string(REPLACE "-D" ";" split_definitions "${definition_part}")
            else()
                set(split_definitions "${definition_part}")
            endif()

            foreach(split_definition IN LISTS split_definitions)
                if(NOT split_definition)
                    continue()
                endif()

                if(NOT split_definition MATCHES "^[A-Za-z_][A-Za-z0-9_]*(=.*)?$")
                    message(FATAL_ERROR
                        "Invalid compile definition detected: '${split_definition}'. "
                        "Use target_compile_definitions(target PRIVATE MACRO or MACRO=value), "
                        "never pass joined command-line fragments such as -DMACRO-DOTHER.")
                endif()

                list(APPEND normalized_definitions "${split_definition}")
            endforeach()
        endforeach()
    endforeach()

    if(normalized_definitions)
        list(REMOVE_DUPLICATES normalized_definitions)
    endif()
    set(${output_variable} "${normalized_definitions}" PARENT_SCOPE)
endfunction()

function(htmsr_normalize_compile_options output_variable)
    set(normalized_options)
    foreach(option IN LISTS ARGN)
        if(NOT option)
            continue()
        endif()

        if(option MATCHES "^-D")
            htmsr_normalize_compile_definitions(split_definitions "${option}")
            foreach(split_definition IN LISTS split_definitions)
                list(APPEND normalized_options "-D${split_definition}")
            endforeach()
        else()
            list(APPEND normalized_options "${option}")
        endif()
    endforeach()

    if(normalized_options)
        list(REMOVE_DUPLICATES normalized_options)
    endif()
    set(${output_variable} "${normalized_options}" PARENT_SCOPE)
endfunction()

function(htmsr_sanitize_imported_compile_definitions)
    foreach(imported_target IN LISTS ARGN)
        if(NOT TARGET "${imported_target}")
            continue()
        endif()

        get_target_property(imported_definitions "${imported_target}" INTERFACE_COMPILE_DEFINITIONS)
        if(imported_definitions)
            htmsr_normalize_compile_definitions(clean_definitions ${imported_definitions})
            set_target_properties("${imported_target}" PROPERTIES
                INTERFACE_COMPILE_DEFINITIONS "${clean_definitions}")
        endif()

        get_target_property(imported_options "${imported_target}" INTERFACE_COMPILE_OPTIONS)
        if(imported_options)
            htmsr_normalize_compile_options(clean_options ${imported_options})
            set_target_properties("${imported_target}" PROPERTIES
                INTERFACE_COMPILE_OPTIONS "${clean_options}")
        endif()
    endforeach()
endfunction()

function(htmsr_generate_msvc_import_lib_from_dll output_variable dll_path library_name)
    set(${output_variable} "" PARENT_SCOPE)
    if(NOT MSVC OR NOT EXISTS "${dll_path}")
        return()
    endif()

    find_program(HTMSR_DUMPBIN_EXECUTABLE dumpbin)
    find_program(HTMSR_LIB_EXECUTABLE lib)
    if(NOT HTMSR_DUMPBIN_EXECUTABLE OR NOT HTMSR_LIB_EXECUTABLE)
        message(WARNING "dumpbin.exe or lib.exe was not found. Cannot generate import library for ${dll_path}.")
        return()
    endif()

    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_import_libs")
    file(MAKE_DIRECTORY "${generated_dir}")
    set(def_file "${generated_dir}/${library_name}.def")
    set(import_lib "${generated_dir}/${library_name}.lib")

    execute_process(
        COMMAND "${HTMSR_DUMPBIN_EXECUTABLE}" /exports "${dll_path}"
        OUTPUT_VARIABLE dumpbin_output
        ERROR_VARIABLE dumpbin_error
        RESULT_VARIABLE dumpbin_result)
    if(NOT dumpbin_result EQUAL 0)
        message(WARNING "dumpbin.exe failed for ${dll_path}: ${dumpbin_error}")
        return()
    endif()

    string(REPLACE "\r\n" "\n" dumpbin_output "${dumpbin_output}")
    string(REPLACE "\r" "\n" dumpbin_output "${dumpbin_output}")
    string(REPLACE "\n" ";" dumpbin_lines "${dumpbin_output}")

    set(exported_symbols)
    foreach(dumpbin_line IN LISTS dumpbin_lines)
        if(dumpbin_line MATCHES "^[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t=]+)")
            list(APPEND exported_symbols "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    if(NOT exported_symbols)
        message(WARNING "No exports were found in ${dll_path}.")
        return()
    endif()

    list(REMOVE_DUPLICATES exported_symbols)
    get_filename_component(dll_filename "${dll_path}" NAME)
    file(WRITE "${def_file}" "LIBRARY ${dll_filename}\nEXPORTS\n")
    foreach(exported_symbol IN LISTS exported_symbols)
        file(APPEND "${def_file}" "    ${exported_symbol}\n")
    endforeach()

    execute_process(
        COMMAND "${HTMSR_LIB_EXECUTABLE}" /nologo /machine:x64 "/def:${def_file}" "/out:${import_lib}"
        ERROR_VARIABLE lib_error
        RESULT_VARIABLE lib_result)
    if(NOT lib_result EQUAL 0 OR NOT EXISTS "${import_lib}")
        message(WARNING "lib.exe failed to generate ${import_lib}: ${lib_error}")
        return()
    endif()

    set(${output_variable} "${import_lib}" PARENT_SCOPE)
endfunction()

function(htmsr_collect_runtime_dlls output_variable)
    set(collected_files)
    foreach(runtime_pattern IN LISTS ARGN)
        file(GLOB pattern_files "${runtime_pattern}")
        foreach(runtime_file IN LISTS pattern_files)
            get_filename_component(runtime_name "${runtime_file}" NAME)
            if(runtime_name MATCHES "^(api-ms-|concrt|msvcp|ucrtbase|vcruntime)")
                continue()
            endif()

            if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT runtime_name MATCHES "d\\.dll$")
                list(APPEND collected_files "${runtime_file}")
            endif()
        endforeach()
    endforeach()

    if(collected_files)
        list(REMOVE_DUPLICATES collected_files)
    endif()
    set(${output_variable} "${collected_files}" PARENT_SCOPE)
endfunction()

function(htmsr_add_runtime_copy_commands target)
    foreach(runtime_file IN LISTS ARGN)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${runtime_file}"
                "$<TARGET_FILE_DIR:${target}>")
    endforeach()
endfunction()

find_package(OpenCV REQUIRED)
if(HTMSR_EIGEN_INCLUDE_DIR)
    if(NOT EXISTS "${HTMSR_EIGEN_INCLUDE_DIR}/Eigen/Core")
        message(FATAL_ERROR "Eigen was not found. Set HTMSR_EIGEN_INCLUDE_DIR in config/environment.ini to a directory that contains Eigen/Core.")
    endif()

    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    target_include_directories(Eigen3::Eigen INTERFACE "${HTMSR_EIGEN_INCLUDE_DIR}")
    message(STATUS "Using Eigen headers from ${HTMSR_EIGEN_INCLUDE_DIR}")
else()
    find_package(Eigen3 REQUIRED)
endif()
# PCL 1.12.1 的 Windows 安装包会在 PCLConfig.cmake 中使用 string(APPEND)
# 追加 BOOST_DEFINITIONS。如果 Boost 包已经设置过同名变量，两个 -D 片段
# 会被粘成 BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB，导致 VS IntelliSense 报 E0992。
# 这里在进入 PCL 配置前清空兼容变量，保留后续 target 级宏定义作为唯一来源。
unset(BOOST_DEFINITIONS)
find_package(PCL REQUIRED COMPONENTS common io)
htmsr_normalize_compile_definitions(HTMSR_PCL_DEFINITIONS ${PCL_DEFINITIONS})
set(PCL_DEFINITIONS "${HTMSR_PCL_DEFINITIONS}")
htmsr_sanitize_imported_compile_definitions(${PCL_LIBRARIES})

set(HTMSR_PCL_CORE_LINK_LIBRARIES)
foreach(pcl_library IN LISTS PCL_LIBRARIES)
    list(APPEND HTMSR_PCL_CORE_LINK_LIBRARIES "$<LINK_ONLY:${pcl_library}>")
endforeach()
find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent OpenGL)

add_library(htmsr_config src/app/services/ConfigFiles.cpp)
target_include_directories(htmsr_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(htmsr_config PUBLIC Qt5::Core)
target_compile_definitions(htmsr_config PRIVATE HTMSR_DEFAULT_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
if(MSVC)
    target_compile_options(htmsr_config PRIVATE /utf-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

add_library(htmsr_core
    src/core/CalibrationService.cpp
    src/core/FileSystemUtils.cpp
    src/core/LaserExtractionService.cpp
    src/core/Logger.cpp
    src/core/PointCloudService.cpp
    src/core/ReconstructionService.cpp
)

target_include_directories(htmsr_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OpenCV_INCLUDE_DIRS}
    PRIVATE
        ${PCL_INCLUDE_DIRS}
)

target_link_libraries(htmsr_core
    PUBLIC
        Eigen3::Eigen
        ${OpenCV_LIBS}
    PRIVATE
        ${HTMSR_PCL_CORE_LINK_LIBRARIES}
)

target_compile_definitions(htmsr_core
    PUBLIC
        BOOST_ALL_NO_LIB
        NOMINMAX
)

if(MSVC)
    target_compile_options(htmsr_core PRIVATE /utf-8)
endif()

set(HTMSR_APP_SOURCES
    src/app/main.cpp
    src/app/acquisition/AcquisitionTypes.cpp
    src/app/acquisition/GalvoController.cpp
    src/app/acquisition/GalvoCaptureSupport.cpp
    src/app/acquisition/HardwareTriggeredStereoCapture.cpp
    src/app/acquisition/MockAcquisitionProvider.cpp
    src/app/acquisition/OfflineImageSequenceProvider.cpp
    src/app/acquisition/HikStereoCameraProvider.cpp
    src/app/services/AcquisitionService.cpp
    src/app/services/AppConfigService.cpp
    src/app/services/FileLogSink.cpp
    src/app/services/CalibrationCaptureSessionService.cpp
    src/app/services/IntegratedCalibrationCaptureService.cpp
    src/app/services/IntegratedScanService.cpp
    src/app/services/QtLogSink.cpp
    src/app/services/ReconstructionCaptureSessionService.cpp
    src/app/services/ReconstructionStorage.cpp
    src/app/services/ResultExportService.cpp
    src/app/ui/AcquisitionPanel.cpp
    src/app/ui/CaptureReviewWidget.cpp
    src/app/ui/ImageViewWidget.cpp
    src/app/ui/LogPanel.cpp
    src/app/ui/MainWindow.cpp
    src/app/ui/ParameterPanel.cpp
    src/app/ui/PointCloudViewWidget.cpp
    src/app/ui/SerialCommandPackWidget.cpp
    src/app/ui/ZoomableImageView.cpp
)

option(HTMSR_BUILD_CAPTURE_TESTS "Build hardware-trigger stereo capture simulation tests" OFF)
if(HTMSR_BUILD_CAPTURE_TESTS)
    find_package(Threads REQUIRED)
    enable_testing()
    add_test(NAME build_environment COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/test/HtmsrBuildConfigTests.cmake)
    add_executable(htmsr_capture_tests
        test/HardwareTriggeredStereoCaptureTests.cpp
        test/GalvoCaptureWorkflowTests.cpp
        src/app/acquisition/HardwareTriggeredStereoCapture.cpp
        src/app/acquisition/GalvoCaptureSupport.cpp
        src/app/acquisition/AcquisitionTypes.cpp
        src/core/Logger.cpp
    )
    set_target_properties(htmsr_capture_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(htmsr_capture_tests PRIVATE src ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(htmsr_capture_tests PRIVATE Eigen3::Eigen ${OpenCV_LIBS} Threads::Threads)
    target_compile_definitions(htmsr_capture_tests PRIVATE NOMINMAX)
    if(MSVC)
        target_compile_options(htmsr_capture_tests PRIVATE /utf-8)
    endif()
    add_test(NAME hardware_trigger_stereo_capture COMMAND htmsr_capture_tests)

    if(WIN32)
        add_executable(htmsr_config_tests
            test/ConfigFilesTests.cpp
            test/AppConfigPersistenceTests.cpp
            src/app/services/AppConfigService.cpp
            src/app/ui/AcquisitionPanel.cpp
            src/app/ui/AcquisitionPanel.h
            src/app/ui/SerialCommandPackWidget.cpp
            src/app/ui/SerialCommandPackWidget.h
            src/app/acquisition/AcquisitionTypes.cpp
            src/app/acquisition/GalvoController.cpp
            src/app/acquisition/GalvoCaptureSupport.cpp
        )
        target_link_libraries(htmsr_config_tests PRIVATE htmsr_core htmsr_config Qt5::Widgets)
        target_compile_definitions(htmsr_config_tests PRIVATE HTMSR_WITH_HIK_CAMERA=0)
        if(MSVC)
            target_compile_options(htmsr_config_tests PRIVATE /utf-8)
        endif()
        add_test(NAME config_files COMMAND htmsr_config_tests)
        set_tests_properties(config_files PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(htmsr_serial_diagnostics_tests
            test/SerialDiagnosticsTests.cpp
            test/AppConfigPersistenceTests.cpp
            test/CalibrationWorkflowTests.cpp
            test/ReconstructionStorageTests.cpp
            test/ResultExportTests.cpp
            src/app/services/AppConfigService.cpp
            src/app/services/CalibrationCaptureSessionService.cpp
            src/app/services/ReconstructionCaptureSessionService.cpp
            src/app/services/ReconstructionStorage.cpp
            src/app/services/ResultExportService.cpp
            src/app/acquisition/MockAcquisitionProvider.cpp
            src/app/acquisition/GalvoController.cpp
            src/app/acquisition/GalvoCaptureSupport.cpp
            src/app/acquisition/AcquisitionTypes.cpp
            src/app/ui/AcquisitionPanel.cpp
            src/app/ui/AcquisitionPanel.h
        )
        target_include_directories(htmsr_serial_diagnostics_tests PRIVATE src)
        target_link_libraries(htmsr_serial_diagnostics_tests PRIVATE htmsr_core htmsr_config Qt5::Widgets)
        target_compile_definitions(htmsr_serial_diagnostics_tests PRIVATE HTMSR_WITH_HIK_CAMERA=0)
        if(MSVC)
            target_compile_options(htmsr_serial_diagnostics_tests PRIVATE /utf-8)
        endif()
        add_test(NAME serial_diagnostics COMMAND htmsr_serial_diagnostics_tests)
        set_tests_properties(serial_diagnostics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(htmsr_logging_tests
            test/LoggingTests.cpp
            src/app/services/FileLogSink.cpp
            src/app/services/QtLogSink.cpp
            src/app/services/QtLogSink.h
            src/app/ui/LogPanel.cpp
            src/app/ui/LogPanel.h
        )
        target_include_directories(htmsr_logging_tests PRIVATE src)
        target_link_libraries(htmsr_logging_tests PRIVATE htmsr_core Qt5::Widgets Threads::Threads)
        if(MSVC)
            target_compile_options(htmsr_logging_tests PRIVATE /utf-8)
        endif()
        add_test(NAME logging COMMAND htmsr_logging_tests)
        set_tests_properties(logging PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

if(HTMSR_ENABLE_HIK_CAMERA)
    set(HIK_MVS_INCLUDE_DIR "${HIK_MVS_ROOT}/Development/Includes" CACHE PATH "Path to Hikrobot MVS include directory" FORCE)
    set(HIK_MVS_LIBRARY "${HIK_MVS_ROOT}/Development/Libraries/win64/MvCameraControl.lib" CACHE FILEPATH "Path to Hikrobot MVS x64 import library" FORCE)

    if(NOT EXISTS "${HIK_MVS_INCLUDE_DIR}/MvCameraControl.h")
        message(FATAL_ERROR "Hikrobot MVS header was not found. Check HIK_MVS_ROOT in config/environment.ini.")
    endif()
    if(NOT EXISTS "${HIK_MVS_LIBRARY}")
        message(FATAL_ERROR "Hikrobot MVS library was not found. Check HIK_MVS_ROOT in config/environment.ini.")
    endif()

    if(HTMSR_BUILD_CAPTURE_TESTS)
        add_executable(htmsr_hik_configuration_tests
            test/HikCameraConfigurationTests.cpp
            src/app/acquisition/HikCameraDevice.cpp
            src/core/Logger.cpp
        )
        set_target_properties(htmsr_hik_configuration_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(htmsr_hik_configuration_tests PRIVATE
            test/fakes src ${OpenCV_INCLUDE_DIRS} "${HIK_MVS_INCLUDE_DIR}")
        target_link_libraries(htmsr_hik_configuration_tests PRIVATE Eigen3::Eigen ${OpenCV_LIBS} Threads::Threads)
        target_compile_definitions(htmsr_hik_configuration_tests PRIVATE NOMINMAX)
        if(MSVC)
            target_compile_options(htmsr_hik_configuration_tests PRIVATE /utf-8)
        endif()
        add_test(NAME hik_camera_configuration COMMAND htmsr_hik_configuration_tests)
    endif()

    list(APPEND HTMSR_APP_SOURCES
        src/app/acquisition/HikCameraDevice.cpp
    )
endif()

add_executable(htmsr_app WIN32 ${HTMSR_APP_SOURCES})

if(WIN32 AND OPENNI2_LIBRARY)
    get_filename_component(HTMSR_OPENNI2_LIBRARY_DIRECTORY "${OPENNI2_LIBRARY}" DIRECTORY)
    set(HTMSR_OPENNI2_RUNTIME "${HTMSR_OPENNI2_LIBRARY_DIRECTORY}/../Redist/OpenNI2.dll")
    if(EXISTS "${HTMSR_OPENNI2_RUNTIME}")
        add_custom_command(TARGET htmsr_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HTMSR_OPENNI2_RUNTIME}" "$<TARGET_FILE_DIR:htmsr_app>"
            COMMENT "Copying configured OpenNI2 runtime")
    endif()
endif()

target_include_directories(htmsr_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PCL_INCLUDE_DIRS}
)

target_link_libraries(htmsr_app
    PRIVATE
        htmsr_core
        htmsr_config
        Qt5::Widgets
        Qt5::Concurrent
        Qt5::OpenGL
)

get_target_property(HTMSR_QT_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
if(HTMSR_QT_QMAKE_EXECUTABLE)
    get_filename_component(HTMSR_QT_BIN_DIR "${HTMSR_QT_QMAKE_EXECUTABLE}" DIRECTORY)
endif()

if(NOT HTMSR_QT_BIN_DIR AND Qt5_DIR)
    get_filename_component(HTMSR_QT_CMAKE_PARENT "${Qt5_DIR}" DIRECTORY)
    get_filename_component(HTMSR_QT_LIB_DIR "${HTMSR_QT_CMAKE_PARENT}" DIRECTORY)
    get_filename_component(HTMSR_QT_ROOT "${HTMSR_QT_LIB_DIR}" DIRECTORY)
    set(HTMSR_QT_BIN_DIR "${HTMSR_QT_ROOT}/bin")
endif()

if(HTMSR_QT_BIN_DIR)
    set(HTMSR_WINDEPLOYQT_EXECUTABLE "${HTMSR_QT_BIN_DIR}/windeployqt.exe")
endif()

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
    CONTENT "[Paths]\nPlugins = .\n")

if(MSVC)
    target_compile_options(htmsr_app PRIVATE /utf-8)
endif()

if(EXISTS "${HTMSR_WINDEPLOYQT_EXECUTABLE}")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(HTMSR_WINDEPLOYQT_MODE "--debug")
    else()
        set(HTMSR_WINDEPLOYQT_MODE "--release")
    endif()

    add_custom_command(TARGET htmsr_app POST_BUILD
        COMMAND "${HTMSR_WINDEPLOYQT_EXECUTABLE}"
            "${HTMSR_WINDEPLOYQT_MODE}"
            --no-translations
            --no-quick-import
            --no-opengl-sw
            --dir "$<TARGET_FILE_DIR:htmsr_app>"
            "$<TARGET_FILE:htmsr_app>"
        COMMENT "Deploying Qt runtime with windeployqt")
else()
    message(WARNING "windeployqt.exe was not found. Visual Studio may rely on PATH to locate Qt runtime DLLs.")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(HTMSR_QT_IMPORTED_CONFIG DEBUG)
else()
    set(HTMSR_QT_IMPORTED_CONFIG RELEASE)
endif()

foreach(qt_target Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Concurrent Qt5::OpenGL)
    get_target_property(HTMSR_QT_IMPORTED_DLL "${qt_target}" "IMPORTED_LOCATION_${HTMSR_QT_IMPORTED_CONFIG}")
    if(HTMSR_QT_IMPORTED_DLL)
        add_custom_command(TARGET htmsr_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HTMSR_QT_IMPORTED_DLL}"
                "$<TARGET_FILE_DIR:htmsr_app>"
            COMMENT "Copying ${qt_target} runtime")
    endif()
endforeach()

if(HTMSR_QT_BIN_DIR)
    get_filename_component(HTMSR_QT_ROOT "${HTMSR_QT_BIN_DIR}" DIRECTORY)
    set(HTMSR_QT_PLUGINS_DIR "${HTMSR_QT_ROOT}/plugins")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(HTMSR_QT_PLATFORM_PLUGIN "qwindowsd.dll")
        set(HTMSR_QT_STYLE_PLUGIN "qwindowsvistastyled.dll")
        set(HTMSR_QT_TEST_PLUGIN "qoffscreend.dll")
    else()
        set(HTMSR_QT_PLATFORM_PLUGIN "qwindows.dll")
        set(HTMSR_QT_STYLE_PLUGIN "qwindowsvistastyle.dll")
        set(HTMSR_QT_TEST_PLUGIN "qoffscreen.dll")
    endif()

    if(HTMSR_BUILD_CAPTURE_TESTS AND WIN32)
        if(NOT EXISTS "${HTMSR_QT_PLUGINS_DIR}/platforms/${HTMSR_QT_TEST_PLUGIN}")
            message(FATAL_ERROR "Qt offscreen test plugin is missing. Check Qt5_DIR in config/environment.ini: ${HTMSR_QT_PLUGINS_DIR}/platforms/${HTMSR_QT_TEST_PLUGIN}")
        endif()
        # 测试插件路径也从所选 Qt 自动推导，避免依赖运行测试前手工设置环境变量。
        set_tests_properties(config_files serial_diagnostics logging PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${HTMSR_QT_PLUGINS_DIR}/platforms"
            TIMEOUT 60)
        foreach(qt_test_target htmsr_config_tests htmsr_serial_diagnostics_tests htmsr_logging_tests)
            target_compile_definitions(${qt_test_target} PRIVATE
                HTMSR_QT_TEST_PLUGIN_ROOT="${HTMSR_QT_PLUGINS_DIR}"
                HTMSR_QT_TEST_PLUGIN="${HTMSR_QT_TEST_PLUGIN}")
            # 仅构建测试目标时也部署它所需的 Qt 运行库和离屏插件。
            foreach(qt_test_library Qt5::Core Qt5::Gui Qt5::Widgets)
                add_custom_command(TARGET ${qt_test_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:${qt_test_library}>" "$<TARGET_FILE_DIR:${qt_test_target}>")
            endforeach()
            add_custom_command(TARGET ${qt_test_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${qt_test_target}>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${HTMSR_QT_PLUGINS_DIR}/platforms/${HTMSR_QT_TEST_PLUGIN}"
                    "$<TARGET_FILE_DIR:${qt_test_target}>/platforms/${HTMSR_QT_TEST_PLUGIN}"
                COMMENT "Deploying Qt offscreen plugin for ${qt_test_target}")
        endforeach()
    endif()

    if(EXISTS "${HTMSR_QT_PLUGINS_DIR}/platforms/${HTMSR_QT_PLATFORM_PLUGIN}")
        add_custom_command(TARGET htmsr_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:htmsr_app>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HTMSR_QT_PLUGINS_DIR}/platforms/${HTMSR_QT_PLATFORM_PLUGIN}"
                "$<TARGET_FILE_DIR:htmsr_app>/platforms/${HTMSR_QT_PLATFORM_PLUGIN}"
            COMMENT "Copying Qt platform plugin")
    endif()

    if(EXISTS "${HTMSR_QT_PLUGINS_DIR}/styles/${HTMSR_QT_STYLE_PLUGIN}")
        add_custom_command(TARGET htmsr_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:htmsr_app>/styles"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HTMSR_QT_PLUGINS_DIR}/styles/${HTMSR_QT_STYLE_PLUGIN}"
                "$<TARGET_FILE_DIR:htmsr_app>/styles/${HTMSR_QT_STYLE_PLUGIN}"
            COMMENT "Copying Qt style plugin")
    endif()
endif()

add_custom_command(TARGET htmsr_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
        "$<TARGET_FILE_DIR:htmsr_app>/qt.conf"
    COMMENT "Copying qt.conf")

if(HTMSR_ENABLE_HIK_CAMERA)
    target_include_directories(htmsr_app PRIVATE "${HIK_MVS_INCLUDE_DIR}")
    target_link_libraries(htmsr_app PRIVATE "${HIK_MVS_LIBRARY}")
    target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_HIK_CAMERA=1)

    if(EXISTS "${HIK_MVS_RUNTIME_DIR}/MvCameraControl.dll")
        file(GLOB HTMSR_HIK_RUNTIME_FILES
            "${HIK_MVS_RUNTIME_DIR}/*.dll"
            "${HIK_MVS_RUNTIME_DIR}/*.cti"
            "${HIK_MVS_RUNTIME_DIR}/*.ini"
            "${HIK_MVS_RUNTIME_DIR}/*.manifest")
        foreach(hik_runtime_file IN LISTS HTMSR_HIK_RUNTIME_FILES)
            get_filename_component(hik_runtime_name "${hik_runtime_file}" NAME)
            if(hik_runtime_name MATCHES "^(api-ms-|concrt|msvcp|ucrtbase|vcruntime)")
                continue()
            endif()

            add_custom_command(TARGET htmsr_app POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${hik_runtime_file}"
                    "$<TARGET_FILE_DIR:htmsr_app>")
        endforeach()
    endif()
else()
    target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_HIK_CAMERA=0)
endif()

if(HTMSR_ENABLE_VTK_VIEWER)
    find_package(VTK QUIET COMPONENTS
        GUISupportQt
        CommonCore
        CommonDataModel
        FiltersGeneral
        RenderingCore
        RenderingOpenGL2
        InteractionStyle
    )

    set(HTMSR_CAN_LINK_VTK_VIEWER TRUE)
    if(VTK_FOUND AND TARGET VTK::GUISupportQt AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        get_target_property(HTMSR_VTK_QT_IMPLIB_RELEASE VTK::GUISupportQt IMPORTED_IMPLIB_RELEASE)
        get_target_property(HTMSR_VTK_QT_LOCATION_RELEASE VTK::GUISupportQt IMPORTED_LOCATION_RELEASE)
        get_target_property(HTMSR_VTK_QT_IMPLIB_DEBUG VTK::GUISupportQt IMPORTED_IMPLIB_DEBUG)
        get_target_property(HTMSR_VTK_QT_LOCATION_DEBUG VTK::GUISupportQt IMPORTED_LOCATION_DEBUG)
        if((NOT HTMSR_VTK_QT_IMPLIB_RELEASE AND NOT HTMSR_VTK_QT_LOCATION_RELEASE) AND HTMSR_VTK_QT_IMPLIB_DEBUG)
            set(HTMSR_VTK_QT_LOCATION_CANDIDATE "")
            if(HTMSR_VTK_QT_LOCATION_DEBUG)
                string(REGEX REPLACE "d\\.dll$" ".dll" HTMSR_VTK_QT_LOCATION_CANDIDATE "${HTMSR_VTK_QT_LOCATION_DEBUG}")
            endif()

            if(EXISTS "${HTMSR_VTK_QT_LOCATION_CANDIDATE}")
                htmsr_generate_msvc_import_lib_from_dll(
                    HTMSR_VTK_QT_GENERATED_IMPLIB
                    "${HTMSR_VTK_QT_LOCATION_CANDIDATE}"
                    "vtkGUISupportQt-9.1")
                if(HTMSR_VTK_QT_GENERATED_IMPLIB)
                    set_target_properties(VTK::GUISupportQt PROPERTIES
                        IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
                        IMPORTED_IMPLIB_RELEASE "${HTMSR_VTK_QT_GENERATED_IMPLIB}"
                        IMPORTED_LOCATION_RELEASE "${HTMSR_VTK_QT_LOCATION_CANDIDATE}")
                    set(HTMSR_VTK_QT_IMPLIB_RELEASE "${HTMSR_VTK_QT_GENERATED_IMPLIB}")
                    set(HTMSR_VTK_QT_LOCATION_RELEASE "${HTMSR_VTK_QT_LOCATION_CANDIDATE}")
                    message(STATUS "Generated Release import library for VTK::GUISupportQt: ${HTMSR_VTK_QT_GENERATED_IMPLIB}")
                endif()
            endif()

            if(NOT HTMSR_VTK_QT_IMPLIB_RELEASE AND NOT HTMSR_VTK_QT_LOCATION_RELEASE)
                message(WARNING "VTK::GUISupportQt only provides a Debug import library in this environment. "
                    "Disabling the embedded VTK viewer for this non-Debug build to avoid loading Qt debug DLLs.")
                set(HTMSR_CAN_LINK_VTK_VIEWER FALSE)
            endif()
        endif()
    endif()

    if(VTK_FOUND AND HTMSR_CAN_LINK_VTK_VIEWER)
        target_link_libraries(htmsr_app PRIVATE ${VTK_LIBRARIES})
        target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_VTK_VIEWER=1)
        vtk_module_autoinit(
            TARGETS htmsr_app
            MODULES ${VTK_LIBRARIES}
        )

        if(WIN32)
            set(HTMSR_VIEWER_RUNTIME_PATTERNS)

            if(PCL_DIR)
                get_filename_component(HTMSR_PCL_CMAKE_DIR "${PCL_DIR}" ABSOLUTE)
                get_filename_component(HTMSR_PCL_ROOT_DIR "${HTMSR_PCL_CMAKE_DIR}" DIRECTORY)
                list(APPEND HTMSR_VIEWER_RUNTIME_PATTERNS
                    "${HTMSR_PCL_ROOT_DIR}/bin/*.dll"
                    "${HTMSR_PCL_ROOT_DIR}/3rdParty/FLANN/bin/*.dll"
                    "${HTMSR_PCL_ROOT_DIR}/3rdParty/Qhull/bin/*.dll"
                    "${HTMSR_PCL_ROOT_DIR}/3rdParty/Boost/lib/*.dll"
                    "${HTMSR_PCL_ROOT_DIR}/3rdParty/OpenNI2/Redist/*.dll")
            endif()

            if(VTK_DIR)
                get_filename_component(HTMSR_VTK_CMAKE_DIR "${VTK_DIR}" ABSOLUTE)
                get_filename_component(HTMSR_VTK_CMAKE_PARENT_DIR "${HTMSR_VTK_CMAKE_DIR}" DIRECTORY)
                get_filename_component(HTMSR_VTK_LIB_DIR "${HTMSR_VTK_CMAKE_PARENT_DIR}" DIRECTORY)
                get_filename_component(HTMSR_VTK_ROOT_DIR "${HTMSR_VTK_LIB_DIR}" DIRECTORY)
                list(APPEND HTMSR_VIEWER_RUNTIME_PATTERNS "${HTMSR_VTK_ROOT_DIR}/bin/*.dll")
            endif()

            htmsr_collect_runtime_dlls(HTMSR_VIEWER_RUNTIME_FILES ${HTMSR_VIEWER_RUNTIME_PATTERNS})
            htmsr_add_runtime_copy_commands(htmsr_app ${HTMSR_VIEWER_RUNTIME_FILES})
        endif()
    else()
        set(HTMSR_VTK_VIEWER_FAILURE_REASON
            "Compatible VTK Qt components were not found. Required components include VTK::GUISupportQt and QVTKOpenGLNativeWidget.")
        if(HTMSR_REQUIRE_VTK_VIEWER)
            message(FATAL_ERROR
                "${HTMSR_VTK_VIEWER_FAILURE_REASON} "
                "Install or build VTK with Qt support for the same Qt/MSVC/configuration, then set VTK_DIR to that VTK package.")
        else()
            message(WARNING "${HTMSR_VTK_VIEWER_FAILURE_REASON} Building with the non-VTK fallback point cloud view.")
            target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_VTK_VIEWER=0)
        endif()
    endif()
else()
    if(HTMSR_REQUIRE_VTK_VIEWER)
        message(FATAL_ERROR "HTMSR_REQUIRE_VTK_VIEWER is ON, but HTMSR_ENABLE_VTK_VIEWER is OFF.")
    endif()
    target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_VTK_VIEWER=0)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source" FILES
    src/core/CalibrationService.cpp
    src/core/FileSystemUtils.cpp
    src/core/LaserExtractionService.cpp
    src/core/Logger.cpp
    src/core/PointCloudService.cpp
    src/core/ReconstructionService.cpp
    src/app/main.cpp
)
