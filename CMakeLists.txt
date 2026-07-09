cmake_minimum_required(VERSION 3.24)

project(HTMSR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HTMSR_ENABLE_VTK_VIEWER "Build the embedded PCL/VTK point cloud viewer" ON)

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

find_package(OpenCV REQUIRED)
find_package(Eigen3 QUIET)
if(NOT Eigen3_FOUND)
    set(HTMSR_EIGEN_INCLUDE_DIR "" CACHE PATH "Path to an Eigen include directory that contains Eigen/Core")
    if(NOT HTMSR_EIGEN_INCLUDE_DIR)
        foreach(candidate
            "C:/ENVIORNMENT/ceresLib/Eigen"
            "C:/ENVIORNMENT/PCL/PCL 1.12.1/3rdParty/Eigen/eigen3"
        )
            if(EXISTS "${candidate}/Eigen/Core")
                set(HTMSR_EIGEN_INCLUDE_DIR "${candidate}" CACHE PATH "Path to an Eigen include directory that contains Eigen/Core" FORCE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT EXISTS "${HTMSR_EIGEN_INCLUDE_DIR}/Eigen/Core")
        message(FATAL_ERROR "Eigen was not found. Set HTMSR_EIGEN_INCLUDE_DIR to a directory that contains Eigen/Core.")
    endif()

    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    target_include_directories(Eigen3::Eigen INTERFACE "${HTMSR_EIGEN_INCLUDE_DIR}")
    message(STATUS "Using Eigen headers from ${HTMSR_EIGEN_INCLUDE_DIR}")
endif()
# PCL 1.12.1 的 Windows 安装包会在 PCLConfig.cmake 中使用 string(APPEND)
# 追加 BOOST_DEFINITIONS。如果 Boost 包已经设置过同名变量，两个 -D 片段
# 会被粘成 BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB，导致 VS IntelliSense 报 E0992。
# 这里在进入 PCL 配置前清空兼容变量，保留后续 target 级宏定义作为唯一来源。
unset(BOOST_DEFINITIONS)
find_package(PCL REQUIRED COMPONENTS common io visualization)
htmsr_normalize_compile_definitions(HTMSR_PCL_DEFINITIONS ${PCL_DEFINITIONS})
set(PCL_DEFINITIONS "${HTMSR_PCL_DEFINITIONS}")
htmsr_sanitize_imported_compile_definitions(${PCL_LIBRARIES})

set(HTMSR_PCL_LINK_LIBRARIES)
foreach(pcl_library IN LISTS PCL_LIBRARIES)
    list(APPEND HTMSR_PCL_LINK_LIBRARIES "$<LINK_ONLY:${pcl_library}>")
endforeach()
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

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
        ${HTMSR_PCL_LINK_LIBRARIES}
)

target_compile_definitions(htmsr_core
    PUBLIC
        BOOST_ALL_NO_LIB
        NOMINMAX
)

if(MSVC)
    target_compile_options(htmsr_core PRIVATE /utf-8)
endif()

add_executable(htmsr_app WIN32
    src/app/main.cpp
    src/app/acquisition/AcquisitionTypes.cpp
    src/app/acquisition/OfflineImageSequenceProvider.cpp
    src/app/services/AppConfigService.cpp
    src/app/services/QtLogSink.cpp
    src/app/ui/ImageViewWidget.cpp
    src/app/ui/LogPanel.cpp
    src/app/ui/MainWindow.cpp
    src/app/ui/ParameterPanel.cpp
    src/app/ui/PointCloudViewWidget.cpp
)

target_include_directories(htmsr_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PCL_INCLUDE_DIRS}
)

target_link_libraries(htmsr_app
    PRIVATE
        htmsr_core
        Qt6::Widgets
        Qt6::Concurrent
)

if(MSVC)
    target_compile_options(htmsr_app PRIVATE /utf-8)
endif()

if(HTMSR_ENABLE_VTK_VIEWER)
    find_package(VTK QUIET COMPONENTS
        GUISupportQt
        RenderingCore
        RenderingOpenGL2
        InteractionStyle
    )

    if(VTK_FOUND)
        target_link_libraries(htmsr_app PRIVATE ${HTMSR_PCL_LINK_LIBRARIES} ${VTK_LIBRARIES})
        target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_VTK_VIEWER=1)
        vtk_module_autoinit(
            TARGETS htmsr_app
            MODULES ${VTK_LIBRARIES}
        )
    else()
        message(WARNING "VTK Qt components were not found. Building with a placeholder point cloud view.")
        target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_VTK_VIEWER=0)
    endif()
else()
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
