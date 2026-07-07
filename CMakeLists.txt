cmake_minimum_required(VERSION 3.21)

project(HTMSR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/utf-8 /EHsc /bigobj)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

set(HTMSR_DEFAULT_QT_ROOT "C:/ENVIORNMENT/qt/6.11.1/msvc2022_64")
if(EXISTS "${HTMSR_DEFAULT_QT_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${HTMSR_DEFAULT_QT_ROOT}")
endif()

if(NOT OpenCV_DIR AND EXISTS "C:/ENVIORNMENT/opencv_450_vs2019/OpenCVConfig.cmake")
    set(OpenCV_DIR "C:/ENVIORNMENT/opencv_450_vs2019" CACHE PATH "OpenCV config root")
endif()

if(NOT PCL_DIR AND EXISTS "C:/ENVIORNMENT/PCL/PCL 1.12.1/cmake/PCLConfig.cmake")
    set(PCL_DIR "C:/ENVIORNMENT/PCL/PCL 1.12.1/cmake" CACHE PATH "PCL config root")
endif()

set(HTMSR_EIGEN_INCLUDE_DIR "C:/ENVIORNMENT/PCL/PCL 1.12.1/3rdParty/Eigen/eigen3")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenCV REQUIRED CONFIG)
find_package(PCL REQUIRED COMPONENTS common io)

set(HTMSR_OPENCV_ROOT "C:/ENVIORNMENT/opencv_450_vs2019")
set(HTMSR_OPENCV_INCLUDE_DIR "${HTMSR_OPENCV_ROOT}/include")
set(HTMSR_OPENCV_LIB_DIR "${HTMSR_OPENCV_ROOT}/x64/vc16/lib")

set(HTMSR_OPENCV_LIBS
    optimized opencv_core450
    optimized opencv_imgproc450
    optimized opencv_imgcodecs450
    optimized opencv_calib3d450
    debug opencv_core450d
    debug opencv_imgproc450d
    debug opencv_imgcodecs450d
    debug opencv_calib3d450d
)

set(HTMSR_SOURCES
    src/main.cpp
    src/project_core/project_document.cpp
    src/project_core/project_service.cpp
    src/project_core/manifest_service.cpp
    src/io_export/opencorr_poi_codec.cpp
    src/io_export/result_exporter.cpp
    src/algorithms/gray_centerline_extractor.cpp
    src/algorithms/steger_centerline_extractor.cpp
    src/algorithms/multithreshold_centerline_extractor.cpp
    src/algorithms/centerline_extractor_factory.cpp
    src/reconstruction_core/stereo_calibration_loader.cpp
    src/reconstruction_core/epipolar_line_matcher.cpp
    src/reconstruction_core/stereo_line_laser_reconstruction_engine.cpp
    src/reconstruction_core/reconstruction_session_service.cpp
    src/visualization/image_view_widget.cpp
    src/visualization/pointcloud_summary_widget.cpp
    src/app_shell/ribbon_widget.cpp
    src/app_shell/main_window.cpp
)

add_executable(HTMSR ${HTMSR_SOURCES})

target_include_directories(HTMSR PRIVATE src)
target_include_directories(HTMSR PRIVATE ${HTMSR_OPENCV_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS})
target_include_directories(HTMSR PRIVATE ${HTMSR_EIGEN_INCLUDE_DIR} ${PCL_INCLUDE_DIRS})
target_link_directories(HTMSR PRIVATE ${HTMSR_OPENCV_LIB_DIR} ${PCL_LIBRARY_DIRS})
target_compile_options(HTMSR PRIVATE ${PCL_DEFINITIONS})

target_link_libraries(HTMSR PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
    ${HTMSR_OPENCV_LIBS}
    ${PCL_LIBRARIES}
)

enable_testing()

add_executable(htmsr_smoke_test
    tests/smoke_test.cpp
    src/project_core/project_document.cpp
    src/project_core/project_service.cpp
    src/project_core/manifest_service.cpp
    src/io_export/opencorr_poi_codec.cpp
)

target_include_directories(htmsr_smoke_test PRIVATE src)
target_link_libraries(htmsr_smoke_test PRIVATE Qt6::Core)

add_test(NAME htmsr_smoke_test COMMAND htmsr_smoke_test)
