# BayerFlow Windows CLI

RAW video temporal denoiser — Windows port of [BayerFlow](https://github.com/kadenlee1107/BayerFlow).

## Supported Formats

| Format | Input | Output |
|--------|-------|--------|
| ProRes RAW (.mov) | ✅ | ✅ |
| Blackmagic RAW (.braw) | ✅ | ✅ |
| GoPro CineForm (.mov) | ✅ | ✅ |
| ARRIRAW (.ari) | ✅ | — |
| Z CAM ZRAW (.mov) | ✅ | — |
| Canon Cinema RAW Light (.crm) | ✅ | — |
| CinemaDNG (folder) | ✅ | ✅ |
| OpenEXR sequence | — | ✅ |

## Build (Windows)

### Prerequisites

1. **Visual Studio 2022** (Community is free) with "Desktop development with C++" workload
2. **CMake 3.20+** — https://cmake.org/download/
3. **vcpkg** — package manager for C++ libraries

```bat
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg integrate install
```

### Install dependencies

```bat
C:\vcpkg\vcpkg install pthreads:x64-windows
```

### Install NVIDIA Optical Flow SDK

1. Download from https://developer.nvidia.com/optical-flow-sdk
2. Extract to `C:\nvof_sdk\` (must contain `Interface\` and `Lib\` subfolders)

### Build

```bat
git clone https://github.com/kadenlee1107/BayerFlow-Win.git
cd BayerFlow-Win
cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DNVOF_SDK_ROOT=C:/nvof_sdk ^
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The binary will be at `build\Release\bayerflow.exe`.

## Usage

```bat
bayerflow.exe --input clip.mov --output clip_denoised.mov

bayerflow.exe --input clip.mov --output clip_denoised.mov ^
    --tf-mode 2 ^
    --strength 1.5 ^
    --window 15 ^
    --frames 100
```

### All Options

```
--input  <path>       Input file (required)
--output <path>       Output file (required)
--frames <N>          Process only first N frames (0 = all)
--start  <N>          Start frame, 0-based (default 0)
--end    <N>          End frame exclusive
--window <N>          Temporal window size (default 15)
--strength <F>        Filter strength (default 1.5)
--spatial <F>         Spatial denoise strength (default 0 = off)
--tf-mode <N>         0 = NLM,  2 = VST+Bilateral (default 2)
--dark-frame <path>   Dark frame .mov for hot pixel removal
--hotpixel <path>     Hot pixel profile .bin
--iso <N>             Sensor ISO
--output-format <N>   0=auto  1=MOV  2=DNG  3=BRAW  4=EXR
--black-level <F>     Sensor black level in 16-bit ADU (default 6032)
--shot-gain <F>       Shot noise gain (default 180)
--read-noise <F>      Read noise floor in 16-bit ADU (default 616)
```

## Performance

| Component | Mac (M-series) | Windows RTX 5070 |
|-----------|---------------|------------------|
| Optical flow | 240ms (ANE, 4 neighbors) | ~8-20ms (NVOF, 4 neighbors) |
| Temporal filter | 70ms (Metal) | ~20ms (CUDA, 3× bandwidth) |
| Wall clock | ~280ms (3.6 fps) | ~50ms (~20 fps estimated) |

CUDA temporal filter and NVOF optical flow are fully implemented.
CPU fallback activates automatically if no CUDA GPU is detected.

## Roadmap

- [x] CUDA temporal filter kernel (`platform/win/temporal_filter.cu`)
- [x] NVIDIA Optical Flow SDK (`platform/win/platform_of_win.c`)
- [ ] ProRes 4444 output via Media Foundation (for R3D output)
- [ ] GUI (Qt)
