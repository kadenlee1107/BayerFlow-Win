cmake_minimum_required(VERSION 3.20)
project(BayerFlowCLI C)

set(CMAKE_C_STANDARD 11)

# ---- Portable core sources (copies of Mac originals, edited independently) ----
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
    set(PLATFORM_SOURCES
        platform/win/platform_of_win.c
        platform/win/platform_gpu_win.c
    )
    # Uncomment when CUDA is ready:
    # enable_language(CUDA)
    # list(APPEND PLATFORM_SOURCES platform/win/temporal_filter.cu)
else()
    set(PLATFORM_SOURCES
        platform/mac/platform_of_mac_stub.c
        platform/mac/platform_gpu_mac_stub.c
    )
endif()

add_executable(bayerflow_cli ${CORE_SOURCES} ${PLATFORM_SOURCES} core/denoise.c)

if(WIN32)
    target_compile_definitions(bayerflow_cli PRIVATE BAYERFLOW_WINDOWS=1)
else()
    target_compile_definitions(bayerflow_cli PRIVATE BAYERFLOW_MAC=1)
    target_link_libraries(bayerflow_cli "-framework Foundation")
endif()

if(MSVC)
    target_compile_options(bayerflow_cli PRIVATE /O2 /arch:AVX2)
else()
    target_compile_options(bayerflow_cli PRIVATE -O3 -march=native)
endif()
