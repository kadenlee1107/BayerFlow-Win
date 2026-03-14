/* temporal_filter.cu — CUDA port of TemporalFilter.metal
 * Direct translation of the 4-pass VST+Bilateral pipeline.
 *
 * Kernel mapping (Metal → CUDA):
 *   vst_bilateral_collect     → __global__ vst_bilateral_collect_k
 *   vst_bilateral_preestimate → __global__ vst_bilateral_preestimate_k
 *   vst_bilateral_fuse        → __global__ vst_bilateral_fuse_k
 *   vst_bilateral_finalize    → __global__ vst_bilateral_finalize_k
 *
 * Metal [[thread_position_in_grid]] → (blockIdx * blockDim + threadIdx)
 * Metal device* → raw CUDA pointer
 * Metal constant* → pass-by-value struct
 * Metal max/min/abs/clamp → fmaxf/fminf/fabsf/inline clamp_f
 */

#include <cuda_runtime.h>
#include <cuda.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Thread block size ---- */
#define BLK_W 16
#define BLK_H 16

/* ---- Params struct (matches Metal VSTBilateralParams) ---- */
struct VSTBilateralParams {
    unsigned int width;
    unsigned int height;
    float noise_sigma;
    float h;
    float z_reject;
    float flow_sigma2;
    float sigma_g2;
    float black_level;
    float shot_gain;
    float read_noise;
};

/* ---- Multi-hypothesis offsets (green-pixel units, matches Metal) ---- */
__constant__ float2 VST_HYPS[4] = {
    {0.0f,  0.0f},
    {0.5f,  0.0f},
    {0.0f,  0.5f},
    {-0.5f, -0.5f}
};

/* ---- Helper: clamp float ---- */
__device__ __forceinline__ float clamp_f(float x, float lo, float hi) {
    return fmaxf(lo, fminf(hi, x));
}

/* ---- Forward Generalized Anscombe Transform ---- */
__device__ __forceinline__
float vst_fwd(float v, float bl, float sg, float rn) {
    float rv = rn * rn;
    float sig = fmaxf(v - bl, 0.0f);
    float x = sig / sg + 0.375f + rv / (sg * sg);
    return 2.0f * sqrtf(fmaxf(x, 0.0f));
}

/* ---- Bilinear warp of same-color Bayer pixel via optical flow ---- */
__device__ __forceinline__
float warp_bayer(unsigned rx, unsigned ry, float fdx, float fdy,
                 const uint16_t * __restrict__ frame, unsigned w, unsigned h)
{
    int ix = (int)floorf(fdx);
    int iy = (int)floorf(fdy);
    float fx = fdx - (float)ix;
    float fy = fdy - (float)iy;

    int bx0 = (int)rx + ix * 2;
    int by0 = (int)ry + iy * 2;
    int bx1 = bx0 + 2;
    int by1 = by0 + 2;

    if (bx0 < 0 || bx1 >= (int)w || by0 < 0 || by1 >= (int)h)
        return -1.0f;

    float s00 = (float)frame[by0 * w + bx0];
    float s10 = (float)frame[by0 * w + bx1];
    float s01 = (float)frame[by1 * w + bx0];
    float s11 = (float)frame[by1 * w + bx1];

    return (1.0f - fx) * (1.0f - fy) * s00
         +         fx  * (1.0f - fy) * s10
         + (1.0f - fx) *         fy  * s01
         +         fx  *         fy  * s11;
}

/* ============================================================
   Pass 1a: Collect
   Dispatched once per neighbor. Accumulates z_sum, z_count, max_flow.
   ============================================================ */
__global__ void vst_bilateral_collect_k(
    const uint16_t * __restrict__ center_frame,
    const uint16_t * __restrict__ neighbor_frame,
    const float    * __restrict__ flow_x,
    const float    * __restrict__ flow_y,
    float          *z_sum,
    float          *z_count,
    float          *max_flow_buf,
    VSTBilateralParams params)
{
    unsigned rx = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned ry = blockIdx.y * blockDim.y + threadIdx.y;
    unsigned w = params.width, h = params.height;
    if (rx >= w || ry >= h) return;

    unsigned gx = rx >> 1, gy = ry >> 1, gw = w >> 1;
    float fdx = flow_x[gy * gw + gx];
    float fdy = flow_y[gy * gw + gx];

    float v = warp_bayer(rx, ry, fdx, fdy, neighbor_frame, w, h);
    if (v < 0.0f) return;

    unsigned idx = ry * w + rx;
    float z_c = vst_fwd((float)center_frame[idx], params.black_level, params.shot_gain, params.read_noise);
    float z_n = vst_fwd(v, params.black_level, params.shot_gain, params.read_noise);

    if (fabsf(z_n - z_c) > params.z_reject) return;

    float diff1 = z_n - z_c;
    float w1 = expf(-diff1 * diff1 / (2.0f * params.h * params.h));
    z_sum[idx]   += w1 * z_n;
    z_count[idx] += w1;

    float fm = sqrtf(fdx * fdx + fdy * fdy);
    if (fm > max_flow_buf[idx]) max_flow_buf[idx] = fm;
}

/* ============================================================
   Pass 1b: Pre-estimate
   z_preest = (z_sum + z_center) / (z_count + 1)
   Zeros z_sum/z_count for Phase 2 reuse.
   ============================================================ */
__global__ void vst_bilateral_preestimate_k(
    const uint16_t * __restrict__ center_frame,
    float          *z_sum,
    float          *z_count,
    float          *z_preest,
    VSTBilateralParams params)
{
    unsigned rx = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned ry = blockIdx.y * blockDim.y + threadIdx.y;
    unsigned w = params.width, h = params.height;
    if (rx >= w || ry >= h) return;

    unsigned idx = ry * w + rx;
    float zs = z_sum[idx];
    float zc = z_count[idx];
    float z_center = vst_fwd((float)center_frame[idx], params.black_level, params.shot_gain, params.read_noise);

    z_preest[idx] = (zs + z_center) / (zc + 1.0f);

    z_sum[idx]   = 0.0f;
    z_count[idx] = 0.0f;
}

/* ============================================================
   Pass 2: Fuse
   Full bilateral with self-guided reference, structural term,
   multi-hypothesis (M=4). Accumulates pixel values (not z-values).
   ============================================================ */
__global__ void vst_bilateral_fuse_k(
    const uint16_t * __restrict__ center_frame,
    const uint16_t * __restrict__ neighbor_frame,
    const float    * __restrict__ flow_x,
    const float    * __restrict__ flow_y,
    float          *val_sum,
    float          *w_sum,
    const float    * __restrict__ z_preest,
    VSTBilateralParams params)
{
    unsigned rx = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned ry = blockIdx.y * blockDim.y + threadIdx.y;
    unsigned w = params.width, h = params.height;
    if (rx >= w || ry >= h) return;

    unsigned gx = rx >> 1, gy = ry >> 1, gw = w >> 1;
    float fdx = flow_x[gy * gw + gx];
    float fdy = flow_y[gy * gw + gx];

    unsigned idx = ry * w + rx;
    float cv_raw = (float)center_frame[idx];
    float z_ref  = z_preest[idx];

    float neg_inv_2h2 = -1.0f / (2.0f * params.h * params.h);
    float flow_mag2   = fdx * fdx + fdy * fdy;
    float w_flow      = expf(-flow_mag2 / params.flow_sigma2);

    /* ---- Structural term ---- */
    float w_struct = 1.0f;
    {
        float sg = params.shot_gain;
        float vst_jac = 2.0f / fmaxf(z_ref * sg, 0.01f);

        float grad_cx = 0.0f, grad_cy = 0.0f;
        if (rx >= 2 && rx + 2 < w) {
            grad_cx = ((float)center_frame[ry * w + rx + 2]
                     - (float)center_frame[ry * w + rx - 2]) * 0.5f * vst_jac;
        }
        if (ry >= 2 && ry + 2 < h) {
            grad_cy = ((float)center_frame[(ry + 2) * w + rx]
                     - (float)center_frame[(ry - 2) * w + rx]) * 0.5f * vst_jac;
        }

        int wx = (int)rx + (int)roundf(fdx) * 2;
        int wy = (int)ry + (int)roundf(fdy) * 2;
        float grad_nx = 0.0f, grad_ny = 0.0f;

        if (wx >= 2 && wx + 2 < (int)w && wy >= 0 && wy < (int)h) {
            grad_nx = ((float)neighbor_frame[wy * w + wx + 2]
                     - (float)neighbor_frame[wy * w + wx - 2]) * 0.5f * vst_jac;
        }
        if (wy >= 2 && wy + 2 < (int)h && wx >= 0 && wx < (int)w) {
            grad_ny = ((float)neighbor_frame[(wy + 2) * w + wx]
                     - (float)neighbor_frame[(wy - 2) * w + wx]) * 0.5f * vst_jac;
        }

        float gd_x = grad_nx - grad_cx;
        float gd_y = grad_ny - grad_cy;
        float grad_diff_sq = gd_x * gd_x + gd_y * gd_y;
        w_struct = expf(-grad_diff_sq / params.sigma_g2);
    }

    /* ---- Multi-hypothesis (M=4) ---- */
    float best_w = -1.0f;
    float best_v =  0.0f;

    for (int m = 0; m < 4; m++) {
        float hdx = fdx + VST_HYPS[m].x;
        float hdy = fdy + VST_HYPS[m].y;

        float v = warp_bayer(rx, ry, hdx, hdy, neighbor_frame, w, h);
        if (v < 0.0f) continue;

        float z = vst_fwd(v, params.black_level, params.shot_gain, params.read_noise);
        float diff = z - z_ref;
        if (fabsf(diff) > params.z_reject) continue;

        float w_photo = expf(diff * diff * neg_inv_2h2);
        float total   = w_photo * w_struct * w_flow;

        if (total > best_w) {
            best_w = total;
            best_v = v;
        }
    }

    if (best_w <= 0.0f) return;

    /* ---- Texture confidence (bright textureless surfaces) ---- */
    float flow_mag = sqrtf(flow_mag2);
    if (cv_raw > 10000.0f && flow_mag > 0.3f) {
        float tsum = cv_raw, tsq = cv_raw * cv_raw, tn = 1.0f;
        if (rx >= 2)       { float tv = (float)center_frame[ry * w + rx - 2]; tsum += tv; tsq += tv * tv; tn++; }
        if (rx + 2 < w)    { float tv = (float)center_frame[ry * w + rx + 2]; tsum += tv; tsq += tv * tv; tn++; }
        if (ry >= 2)       { float tv = (float)center_frame[(ry - 2) * w + rx]; tsum += tv; tsq += tv * tv; tn++; }
        if (ry + 2 < h)    { float tv = (float)center_frame[(ry + 2) * w + rx]; tsum += tv; tsq += tv * tv; tn++; }

        float tvar = tsq / tn - (tsum / tn) * (tsum / tn);
        float tstd = sqrtf(fmaxf(tvar, 0.0f));

        float noise_std = sqrtf(params.read_noise * params.read_noise
                              + params.shot_gain * fmaxf(cv_raw - params.black_level, 0.0f));
        float tratio = tstd / fmaxf(noise_std, 1.0f);

        float tex_conf    = clamp_f((tratio - 0.8f) / 1.5f, 0.1f, 1.0f);
        float motion_scale = clamp_f(flow_mag / 2.0f, 0.0f, 1.0f);
        tex_conf = 1.0f - (1.0f - tex_conf) * motion_scale;

        best_w *= tex_conf;
    }

    val_sum[idx] += best_w * best_v;
    w_sum[idx]   += best_w;
}

/* ============================================================
   Pass 3: Finalize
   Center weight floor + pixel-space weighted mean → uint16 output.
   ============================================================ */
__global__ void vst_bilateral_finalize_k(
    const uint16_t * __restrict__ center_frame,
    const float    * __restrict__ val_sum,
    const float    * __restrict__ w_sum,
    const float    * __restrict__ max_flow_buf,
    uint16_t       *output,
    VSTBilateralParams params)
{
    unsigned rx = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned ry = blockIdx.y * blockDim.y + threadIdx.y;
    unsigned w = params.width, h = params.height;
    if (rx >= w || ry >= h) return;

    unsigned idx = ry * w + rx;
    float cv       = (float)center_frame[idx];
    float nb_wsum  = w_sum[idx];
    float nb_wzsum = val_sum[idx];

    if (nb_wsum <= 0.0f) {
        output[idx] = (uint16_t)cv;
        return;
    }

    float mf           = max_flow_buf[idx];
    float center_floor = 0.3f + 0.3f * fminf(mf / 3.0f, 1.0f);
    float center_w     = 1.0f;

    float center_frac = center_w / (center_w + nb_wsum);
    if (center_frac < center_floor) {
        float scale = center_w * (1.0f - center_floor) / (center_floor * nb_wsum);
        nb_wsum  *= scale;
        nb_wzsum *= scale;
    }

    float total_w  = center_w + nb_wsum;
    float total_wv = center_w * cv + nb_wzsum;
    float v_est    = total_wv / total_w;

    float result = clamp_f(v_est, 0.0f, 65535.0f);
    output[idx]  = (uint16_t)(result + 0.5f);
}

/* ============================================================
   Host-side ring buffer and launch logic
   ============================================================ */

#define MAX_RING_SLOTS 32

static int      g_num_slots  = 0;
static int      g_width      = 0;
static int      g_height     = 0;
static uint16_t *g_frame_ring[MAX_RING_SLOTS];
static uint16_t *g_denoised_ring[MAX_RING_SLOTS];

/* GPU-side accumulator buffers (reused across frames) */
static float *g_z_sum      = NULL;
static float *g_z_count    = NULL;
static float *g_z_preest   = NULL;
static float *g_val_sum    = NULL;
static float *g_w_sum      = NULL;
static float *g_max_flow   = NULL;

/* Async stream + sync */
static cudaStream_t g_stream      = NULL;
static cudaEvent_t  g_done_event  = NULL;
static int          g_async_pending = 0;
static uint16_t    *g_async_output  = NULL;

static int cuda_ok(cudaError_t e, const char *msg) {
    if (e != cudaSuccess) {
        fprintf(stderr, "CUDA error %s: %s\n", msg, cudaGetErrorString(e));
        return 0;
    }
    return 1;
}

/* Called from platform_gpu_win.c */
extern "C" int bf_cuda_init(int num_slots, int width, int height) {
    /* Free previous allocations */
    for (int i = 0; i < g_num_slots; i++) {
        if (g_frame_ring[i])    cudaFree(g_frame_ring[i]);
        if (g_denoised_ring[i]) cudaFree(g_denoised_ring[i]);
        g_frame_ring[i] = g_denoised_ring[i] = NULL;
    }
    if (g_z_sum)    { cudaFree(g_z_sum);    g_z_sum = NULL; }
    if (g_z_count)  { cudaFree(g_z_count);  g_z_count = NULL; }
    if (g_z_preest) { cudaFree(g_z_preest); g_z_preest = NULL; }
    if (g_val_sum)  { cudaFree(g_val_sum);  g_val_sum = NULL; }
    if (g_w_sum)    { cudaFree(g_w_sum);    g_w_sum = NULL; }
    if (g_max_flow) { cudaFree(g_max_flow); g_max_flow = NULL; }
    if (g_stream)   { cudaStreamDestroy(g_stream); g_stream = NULL; }
    if (g_done_event) { cudaEventDestroy(g_done_event); g_done_event = NULL; }

    g_num_slots = (num_slots > MAX_RING_SLOTS) ? MAX_RING_SLOTS : num_slots;
    g_width  = width;
    g_height = height;

    size_t frame_bytes = (size_t)width * height * sizeof(uint16_t);
    size_t acc_bytes   = (size_t)width * height * sizeof(float);

    /* cudaMallocManaged: unified memory — CPU writes, GPU reads, no explicit copy */
    for (int i = 0; i < g_num_slots; i++) {
        if (!cuda_ok(cudaMallocManaged(&g_frame_ring[i],    frame_bytes), "frame_ring"))    return 0;
        if (!cuda_ok(cudaMallocManaged(&g_denoised_ring[i], frame_bytes), "denoised_ring")) return 0;
    }

    /* Accumulator buffers live only on GPU */
    if (!cuda_ok(cudaMalloc(&g_z_sum,    acc_bytes), "z_sum"))    return 0;
    if (!cuda_ok(cudaMalloc(&g_z_count,  acc_bytes), "z_count"))  return 0;
    if (!cuda_ok(cudaMalloc(&g_z_preest, acc_bytes), "z_preest")) return 0;
    if (!cuda_ok(cudaMalloc(&g_val_sum,  acc_bytes), "val_sum"))  return 0;
    if (!cuda_ok(cudaMalloc(&g_w_sum,    acc_bytes), "w_sum"))    return 0;
    if (!cuda_ok(cudaMalloc(&g_max_flow, acc_bytes), "max_flow")) return 0;

    if (!cuda_ok(cudaStreamCreate(&g_stream),              "stream"))     return 0;
    if (!cuda_ok(cudaEventCreate(&g_done_event),           "done_event")) return 0;

    return 1;
}

extern "C" uint16_t *bf_cuda_ring_frame_ptr(int slot) {
    if (slot < 0 || slot >= g_num_slots) return NULL;
    return g_frame_ring[slot];
}

extern "C" uint16_t *bf_cuda_ring_denoised_ptr(int slot) {
    if (slot < 0 || slot >= g_num_slots) return NULL;
    return g_denoised_ring[slot];
}

extern "C" int bf_cuda_available(void) {
    int count = 0;
    return (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) ? 1 : 0;
}

/* Helper: upload flow (CPU → GPU) */
static float *g_flow_x_gpu = NULL;
static float *g_flow_y_gpu = NULL;
static size_t g_flow_alloc = 0;

static float *get_flow_gpu(const float *cpu_flow, int n_pixels) {
    /* Just return host pointer — unified memory on newer GPUs.
     * For discrete GPUs with separate VRAM, replace with cudaMalloc + cudaMemcpy. */
    return (float *)cpu_flow;
}

static void launch_vst_bilateral(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors, int commit_only)
{
    VSTBilateralParams p;
    p.width       = (unsigned)width;
    p.height      = (unsigned)height;
    p.noise_sigma = noise_sigma;
    p.h           = 1.0f;        /* analytically derived from VST theory */
    p.z_reject    = 3.0f;
    p.flow_sigma2 = 8.0f;
    p.sigma_g2    = 0.5f;
    p.black_level = black_level;
    p.shot_gain   = shot_gain;
    p.read_noise  = read_noise;

    dim3 blk(BLK_W, BLK_H);
    dim3 grd((width  + BLK_W - 1) / BLK_W,
             (height + BLK_H - 1) / BLK_H);
    size_t npix = (size_t)width * height;

    /* Ensure unified memory is visible to GPU before kernel launch */
    cudaStreamSynchronize(g_stream);

    /* Clear accumulators */
    cudaMemsetAsync(g_z_sum,    0, npix * sizeof(float), g_stream);
    cudaMemsetAsync(g_z_count,  0, npix * sizeof(float), g_stream);
    cudaMemsetAsync(g_z_preest, 0, npix * sizeof(float), g_stream);
    cudaMemsetAsync(g_val_sum,  0, npix * sizeof(float), g_stream);
    cudaMemsetAsync(g_w_sum,    0, npix * sizeof(float), g_stream);
    cudaMemsetAsync(g_max_flow, 0, npix * sizeof(float), g_stream);

    const uint16_t *center = use_denoised[center_idx]
        ? g_denoised_ring[ring_slots[center_idx]]
        : g_frame_ring[ring_slots[center_idx]];

    /* ---- Pass 1a: Collect (one dispatch per neighbor) ---- */
    int neighbor_count = 0;
    for (int dist = 1; dist <= num_frames; dist++) {
        if (neighbor_count >= max_neighbors) break;
        for (int sign = -1; sign <= 1; sign += 2) {
            if (neighbor_count >= max_neighbors) break;
            int f = center_idx + dist * sign;
            if (f < 0 || f >= num_frames) continue;
            if (!flows_x[f] || !flows_y[f]) continue;

            const uint16_t *nbr = use_denoised[f]
                ? g_denoised_ring[ring_slots[f]]
                : g_frame_ring[ring_slots[f]];

            vst_bilateral_collect_k<<<grd, blk, 0, g_stream>>>(
                center, nbr,
                flows_x[f], flows_y[f],
                g_z_sum, g_z_count, g_max_flow, p);

            neighbor_count++;
        }
    }

    /* ---- Pass 1b: Pre-estimate ---- */
    vst_bilateral_preestimate_k<<<grd, blk, 0, g_stream>>>(
        center, g_z_sum, g_z_count, g_z_preest, p);

    /* ---- Pass 2: Fuse (one dispatch per neighbor) ---- */
    neighbor_count = 0;
    for (int dist = 1; dist <= num_frames; dist++) {
        if (neighbor_count >= max_neighbors) break;
        for (int sign = -1; sign <= 1; sign += 2) {
            if (neighbor_count >= max_neighbors) break;
            int f = center_idx + dist * sign;
            if (f < 0 || f >= num_frames) continue;
            if (!flows_x[f] || !flows_y[f]) continue;

            const uint16_t *nbr = use_denoised[f]
                ? g_denoised_ring[ring_slots[f]]
                : g_frame_ring[ring_slots[f]];

            vst_bilateral_fuse_k<<<grd, blk, 0, g_stream>>>(
                center, nbr,
                flows_x[f], flows_y[f],
                g_val_sum, g_w_sum, g_z_preest, p);

            neighbor_count++;
        }
    }

    /* ---- Pass 3: Finalize ---- */
    /* Write to a managed output buffer so CPU can read without explicit copy */
    uint16_t *gpu_out;
    cudaMallocManaged(&gpu_out, npix * sizeof(uint16_t));

    vst_bilateral_finalize_k<<<grd, blk, 0, g_stream>>>(
        center, g_val_sum, g_w_sum, g_max_flow, gpu_out, p);

    if (commit_only) {
        /* Async: record event, CPU continues. waitGPU() copies output. */
        cudaEventRecord(g_done_event, g_stream);
        g_async_pending = 1;
        g_async_output  = output;
        /* Store gpu_out pointer for wait */
        static uint16_t *s_gpu_out = NULL;
        if (s_gpu_out) cudaFree(s_gpu_out);
        s_gpu_out = gpu_out;
    } else {
        /* Sync: wait for GPU, copy output to CPU buffer */
        cudaStreamSynchronize(g_stream);
        cudaMemcpy(output, gpu_out, npix * sizeof(uint16_t), cudaMemcpyDeviceToHost);
        cudaFree(gpu_out);
    }
}

extern "C" void bf_cuda_temporal_vst_bilateral(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    launch_vst_bilateral(output, ring_slots, use_denoised, flows_x, flows_y,
                         num_frames, center_idx, width, height,
                         noise_sigma, black_level, shot_gain, read_noise,
                         max_neighbors, 0);
}

extern "C" void bf_cuda_temporal_vst_bilateral_commit(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    launch_vst_bilateral(output, ring_slots, use_denoised, flows_x, flows_y,
                         num_frames, center_idx, width, height,
                         noise_sigma, black_level, shot_gain, read_noise,
                         max_neighbors, 1);
}

extern "C" int bf_cuda_temporal_wait(void) {
    if (!g_async_pending) return 1;
    cudaError_t e = cudaEventSynchronize(g_done_event);
    g_async_pending = 0;
    return (e == cudaSuccess) ? 1 : 0;
}
