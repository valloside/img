cmake_minimum_required(VERSION 3.9.0)
project(img VERSION 0.1.0 LANGUAGES CXX)

include(CheckIPOSupported)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /source-charset:utf-8 /execution-charset:utf-8")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wno-deprecated-enum-enum-conversion")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftree-vectorize -ftree-vectorizer-verbose=9 -fvect-cost-model=unlimited -ffast-math")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(WITH_FFMPEG OFF CACHE BOOL "Disable FFmpeg support in OpenCV")

check_ipo_supported(RESULT result OUTPUT output)

if(result)
    message(STATUS "IPO is supported: ${output}")
    message(STATUS "LTO will enabled for release mode")

    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(WARNING "IPO is not supported: ${output}")
endif()

add_subdirectory(ext/opencv)

set(GENERATED_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include/opencv2)
file(MAKE_DIRECTORY ${GENERATED_INCLUDE_DIR})
configure_file(${CMAKE_BINARY_DIR}/opencv2/cvconfig.h ${GENERATED_INCLUDE_DIR}/cvconfig.h COPYONLY)
configure_file(${CMAKE_BINARY_DIR}/opencv2/opencv_modules.hpp ${GENERATED_INCLUDE_DIR}/opencv_modules.hpp COPYONLY)

include_directories(
    ext/opencv/include
    ext/opencv/modules/calib3d/include
    ext/opencv/modules/core/include
    ext/opencv/modules/dnn/include
    ext/opencv/modules/features2d/include
    ext/opencv/modules/flann/include
    ext/opencv/modules/gapi/include
    ext/opencv/modules/highgui/include
    ext/opencv/modules/imgcodecs/include
    ext/opencv/modules/imgproc/include
    ext/opencv/modules/java/include
    ext/opencv/modules/js/include
    ext/opencv/modules/ml/include
    ext/opencv/modules/objc/include
    ext/opencv/modules/objdetect/include
    ext/opencv/modules/photo/include
    ext/opencv/modules/python/include
    ext/opencv/modules/stitching/include
    ext/opencv/modules/ts/include
    ext/opencv/modules/video/include
    ext/opencv/modules/videoio/include
    ext/opencv/modules/world/include
    ext/imgui
    include
)

file(GLOB_RECURSE native_srcs src/*.cpp)
file(GLOB_RECURSE imgui_srcs ext/imgui/*.cpp)
add_executable(img ${native_srcs} ${imgui_srcs})

target_link_libraries(img PRIVATE opencv_core opencv_imgcodecs opencv_imgproc d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib Dwmapi.lib)