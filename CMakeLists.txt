cmake_minimum_required(VERSION 3.24)

project(HTMSR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HTMSR_ENABLE_VTK_VIEWER "Build the embedded PCL/VTK point cloud viewer" ON)

find_package(OpenCV REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(PCL REQUIRED COMPONENTS common io visualization)
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
        ${PCL_INCLUDE_DIRS}
)

target_link_libraries(htmsr_core
    PUBLIC
        Eigen3::Eigen
        ${OpenCV_LIBS}
        ${PCL_LIBRARIES}
)

if(PCL_DEFINITIONS)
    target_compile_options(htmsr_core PUBLIC ${PCL_DEFINITIONS})
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
)

target_link_libraries(htmsr_app
    PRIVATE
        htmsr_core
        Qt6::Widgets
        Qt6::Concurrent
)

if(HTMSR_ENABLE_VTK_VIEWER)
    find_package(VTK QUIET COMPONENTS
        GUISupportQt
        RenderingCore
        RenderingOpenGL2
        InteractionStyle
    )

    if(VTK_FOUND)
        target_link_libraries(htmsr_app PRIVATE ${VTK_LIBRARIES})
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
