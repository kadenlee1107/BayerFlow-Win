cmake_minimum_required(VERSION 3.20)
project(BayerFlowCLI C CXX CUDA)

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
    core/motion_est.c
    core/dng_reader.c
    core/dng_writer.c
    core/exr_writer.c
    core/mxf_reader.c
    core/denoise.c
)

include_directories(include)

# ---- Platform layer ----
if(WIN32)
    # CUDA + NVOF
    find_package(CUDAToolkit REQUIRED)

    set(PLATFORM_SOURCES
        platform/win/platform_of_win.c
        platform/win/platform_gpu_win.c
        platform/win/platform_stubs.c
        platform/win/temporal_filter.cu
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

# ---- Executable ----
add_executable(bayerflow
    cli/main.c
    ${CORE_SOURCES}
    ${PLATFORM_SOURCES}
)

# ---- CUDA properties ----
if(WIN32)
    set_target_properties(bayerflow PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES "75;80;86;89;90;100"  # Turing, Ampere, Ada, Hopper, Blackwell (RTX 5000)
    )
endif()

# ---- Link libraries ----
if(WIN32)
    target_compile_definitions(bayerflow PRIVATE BAYERFLOW_WINDOWS=1)

    # CUDA runtime
    target_link_libraries(bayerflow CUDA::cudart CUDA::cuda_driver)

    # NVOF SDK 5.x: header-only — runtime loads nvofapi64.dll from NVIDIA driver
    # No .lib to link; LoadLibrary("nvofapi64.dll") is called at startup.

    # pthreads via vcpkg
    find_package(pthreads QUIET)
    if(pthreads_FOUND)
        target_link_libraries(bayerflow PThreads4W::PThreads4W)
    endif()

    target_link_libraries(bayerflow ws2_32)

else()
    target_compile_definitions(bayerflow PRIVATE BAYERFLOW_MAC=1)
    target_link_libraries(bayerflow pthread m)
endif()

# ---- Optimization + MSVC compat ----
if(MSVC)
    target_compile_options(bayerflow PRIVATE
        # Force-include compat_posix.h: provides M_PI, popen, fseeko, strcasecmp,
        # mkdir, __builtin_clz, and suppresses CRT deprecation warnings.
        $<$<COMPILE_LANGUAGE:C>:/FI "compat_posix.h" /O2 /arch:AVX2 /W3>
        $<$<COMPILE_LANGUAGE:CXX>:/O2 /arch:AVX2>
    )
else()
    target_compile_options(bayerflow PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-O3 -march=native -Wall -Wno-unused-function>
        $<$<COMPILE_LANGUAGE:CXX>:-O3 -march=native>
    )
endif()
