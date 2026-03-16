cmake_minimum_required(VERSION 3.20)
project(BayerFlow C CXX CUDA)

set(CMAKE_C_STANDARD 17)   # MSVC needs C17 for <stdatomic.h> support
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)

# ---- NVOF SDK path (download from https://developer.nvidia.com/optical-flow-sdk) ----
set(NVOF_SDK_ROOT "C:/nvof_sdk" CACHE PATH "NVIDIA Optical Flow SDK root")

# ---- Portable core sources ----
set(CORE_SOURCES
    core/dct.c
    core/tile_encoder.c
    core/frame_encoder.c
    core/rice_golomb.c
    core/mov_writer.c
    core/mov_reader.c
    core/prores_raw_dec.c
    core/denoise_core.c
    core/temporal_techniques.c
    core/spatial_denoise.c
    core/temporal_filter.c
    core/braw_dec.c
    core/braw_enc.c
    core/braw_writer.c
    core/cineform_dec.c
    core/cineform_enc.c
    core/crm_dec.c
    core/ari_reader.c
    core/zraw_dec.c
    core/frame_reader.c
    core/hotpixel_profile.c
    core/noise_profile.c
    platform/win/raft_onnx.c
    core/motion_est.c
    core/dng_reader.c
    core/dng_writer.c
    core/exr_writer.c
    core/mxf_reader.c
    core/denoise.c
)

include_directories(include)
include_directories("C:/Users/kaden/onnxruntime-gpu/onnxruntime-win-x64-gpu-1.24.3/include")

# ---- Platform layer ----
if(WIN32)
    # CUDA + NVOF
    find_package(CUDAToolkit REQUIRED)

    set(PLATFORM_SOURCES
        platform/win/platform_of_win.c
        platform/win/platform_gpu_win.c
        platform/win/platform_stubs.c
        platform/win/temporal_filter.cu
        platform/win/dncnn_cuda.cu
    )

    include_directories(
        ${CUDAToolkit_INCLUDE_DIRS}
        ${NVOF_SDK_ROOT}/NvOFInterface
    )

else()
    set(PLATFORM_SOURCES
        platform/mac/platform_of_mac_stub.c
        platform/mac/platform_gpu_mac_stub.c
        platform/mac/platform_stubs.c
    )
endif()

# ---- Static library (shared by CLI and GUI) ----
add_library(bayerflow_core STATIC
    ${CORE_SOURCES}
    ${PLATFORM_SOURCES}
)

if(WIN32)
    set_target_properties(bayerflow_core PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES "75;80;86;89;90;100"
    )
    target_compile_definitions(bayerflow_core PUBLIC BAYERFLOW_WINDOWS=1)
    target_link_libraries(bayerflow_core CUDA::cudart CUDA::cuda_driver)



    find_package(pthreads QUIET)
    if(pthreads_FOUND)
        target_link_libraries(bayerflow_core PThreads4W::PThreads4W)
    endif()

    target_link_libraries(bayerflow_core ws2_32)
    target_link_libraries(bayerflow_core "C:/Users/kaden/onnxruntime-gpu/onnxruntime-win-x64-gpu-1.24.3/lib/onnxruntime.lib")
else()
    target_compile_definitions(bayerflow_core PUBLIC BAYERFLOW_MAC=1)
    target_link_libraries(bayerflow_core pthread m)
endif()

if(MSVC)
    target_compile_options(bayerflow_core PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/FI "compat_posix.h" /O2 /arch:AVX2 /W3>
        $<$<COMPILE_LANGUAGE:CXX>:/FI "compat_posix.h" /O2 /arch:AVX2>
    )
else()
    target_compile_options(bayerflow_core PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-O3 -march=native -Wall -Wno-unused-function>
        $<$<COMPILE_LANGUAGE:CXX>:-O3 -march=native>
    )
endif()

# ---- CLI executable ----
add_executable(bayerflow cli/main.c)
target_link_libraries(bayerflow bayerflow_core)

# ---- Qt GUI (optional) ----
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    message(STATUS "Qt6 found — building bayerflow_gui")
    set(CMAKE_AUTOMOC ON)

    add_executable(bayerflow_gui WIN32
        gui/main.cpp
        gui/MainWindow.cpp
        gui/PreviewWidget.cpp
        gui/DenoiseWorker.cpp
        gui/BayerConverter.cpp
    )
    target_link_libraries(bayerflow_gui bayerflow_core Qt6::Widgets)

    if(MSVC)
        target_compile_options(bayerflow_gui PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/FI "compat_posix.h" /O2 /arch:AVX2>
        )
    endif()
else()
    message(STATUS "Qt6 Widgets not found — skipping Widgets GUI")
endif()

# ---- Qt QML GUI (modern, optional) ----
find_package(Qt6Quick QUIET PATHS "C:/Qt/6.10.2/msvc2022_64/lib/cmake/Qt6Quick")
find_package(Qt6QuickControls2 QUIET PATHS "C:/Qt/6.10.2/msvc2022_64/lib/cmake/Qt6QuickControls2")
if(Qt6Quick_FOUND AND Qt6QuickControls2_FOUND)
    message(STATUS "Qt6 Quick found — building bayerflow_qml")
    set(CMAKE_AUTOMOC ON)

    add_executable(bayerflow_qml WIN32
        qml/main.cpp
        qml/Backend.cpp
        qml/CubeLUT.cpp
    )
    target_link_libraries(bayerflow_qml bayerflow_core Qt6::Quick Qt6::QuickControls2 Qt6::Qml)

    if(MSVC)
        target_compile_options(bayerflow_qml PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/FI "compat_posix.h" /O2 /arch:AVX2>
        )
    endif()
else()
    message(STATUS "Qt6 Quick not found — skipping QML GUI (install Qt Quick module)")
endif()
