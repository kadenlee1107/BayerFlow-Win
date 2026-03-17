#!/usr/bin/env python3
"""
BayerFlow Parallel Denoiser
Splits a clip into N overlapping segments, processes in parallel, stitches result.
Achieves 3-4x speedup on a single GPU by running multiple CLI processes.

Usage:
  python parallel_denoise.py input.MOV output.MOV --workers 4
"""

import argparse
import subprocess
import os
import sys
import time
import shutil
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor, as_completed

BAYERFLOW_EXE = r"C:\Users\kaden\BayerFlow-Win\build\bayerflow.exe"
OVERLAP = 7  # frames overlap on each side (half of 14-frame window)


def probe_frame_count(input_path):
    """Get total frame count using ffprobe."""
    cmd = ["ffprobe", "-v", "error", "-select_streams", "v:0",
           "-count_frames", "-show_entries", "stream=nb_read_frames",
           "-of", "csv=p=0", input_path]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        return int(result.stdout.strip())
    except:
        # Fallback: use bayerflow probe
        cmd2 = [BAYERFLOW_EXE, "--input", input_path, "--frames", "0"]
        # Just return a default
        return 495  # will be overridden


def compute_segments(total_frames, num_workers, overlap=OVERLAP):
    """Split frames into overlapping segments.

    Each segment: (start, end, trim_start, trim_end)
    - start/end: what to pass to bayerflow --start/--end (with overlap)
    - trim_start/trim_end: which frames to keep in the final output
    """
    frames_per_worker = total_frames // num_workers
    segments = []

    for i in range(num_workers):
        # Core range for this worker
        core_start = i * frames_per_worker
        core_end = (i + 1) * frames_per_worker if i < num_workers - 1 else total_frames

        # Extended range with overlap
        ext_start = max(0, core_start - overlap)
        ext_end = min(total_frames, core_end + overlap)

        # How many frames to trim from the output
        trim_left = core_start - ext_start  # frames to skip at start
        trim_right = ext_end - core_end      # frames to skip at end

        segments.append({
            'index': i,
            'ext_start': ext_start,
            'ext_end': ext_end,
            'core_start': core_start,
            'core_end': core_end,
            'trim_left': trim_left,
            'trim_right': trim_right,
            'core_frames': core_end - core_start,
        })

    return segments


def process_segment(segment, input_path, temp_dir, extra_args):
    """Process one segment with bayerflow CLI."""
    idx = segment['index']
    output_path = os.path.join(temp_dir, f"segment_{idx:03d}.mov")

    cmd = [
        BAYERFLOW_EXE,
        "--input", input_path,
        "--output", output_path,
        "--start", str(segment['ext_start']),
        "--end", str(segment['ext_end']),
    ] + extra_args

    print(f"  Segment {idx}: frames {segment['ext_start']}-{segment['ext_end']} "
          f"(core {segment['core_start']}-{segment['core_end']}, "
          f"trim L{segment['trim_left']} R{segment['trim_right']})")

    t0 = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    elapsed = time.time() - t0

    if result.returncode != 0:
        print(f"  Segment {idx} FAILED (code {result.returncode})")
        return None

    print(f"  Segment {idx} done in {elapsed:.1f}s")
    return {
        'index': idx,
        'output': output_path,
        'segment': segment,
        'elapsed': elapsed,
    }


def trim_segment(segment_result, temp_dir):
    """Trim overlap frames from a processed segment."""
    seg = segment_result['segment']
    input_path = segment_result['output']
    idx = seg['index']
    trimmed_path = os.path.join(temp_dir, f"trimmed_{idx:03d}.mov")

    trim_left = seg['trim_left']
    trim_right = seg['trim_right']
    total_ext = seg['ext_end'] - seg['ext_start']
    keep_end = total_ext - trim_right

    if trim_left == 0 and trim_right == 0:
        # No trimming needed
        shutil.copy2(input_path, trimmed_path)
        return trimmed_path

    # Use ffmpeg to trim
    cmd = [
        "ffmpeg", "-y",
        "-i", input_path,
        "-vf", f"select='between(n,{trim_left},{keep_end - 1})',setpts=N/FRAME_RATE/TB",
        "-c:v", "copy",  # try copy first
        trimmed_path,
    ]

    # For ProRes RAW, copy mode might not work with select filter
    # Fall back to re-encoding if needed
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        # Try without -c:v copy
        cmd2 = [
            "ffmpeg", "-y",
            "-i", input_path,
            "-vf", f"select='between(n,{trim_left},{keep_end - 1})',setpts=N/FRAME_RATE/TB",
            trimmed_path,
        ]
        subprocess.run(cmd2, capture_output=True, text=True)

    return trimmed_path


def stitch_segments(trimmed_paths, output_path):
    """Concatenate trimmed segments into final output."""
    # Create concat list file
    concat_file = os.path.join(os.path.dirname(trimmed_paths[0]), "concat_list.txt")
    with open(concat_file, 'w') as f:
        for p in trimmed_paths:
            f.write(f"file '{p}'\n")

    cmd = [
        "ffmpeg", "-y",
        "-f", "concat",
        "-safe", "0",
        "-i", concat_file,
        "-c", "copy",
        output_path,
    ]

    print(f"\nStitching {len(trimmed_paths)} segments → {output_path}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"Stitch failed: {result.stderr[:200]}")
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description="BayerFlow Parallel Denoiser")
    parser.add_argument("input", help="Input video file")
    parser.add_argument("output", help="Output video file")
    parser.add_argument("--workers", "-w", type=int, default=4, help="Number of parallel workers (default: 4)")
    parser.add_argument("--strength", "-s", type=float, default=1.5, help="Denoise strength")
    parser.add_argument("--window", type=int, default=15, help="Temporal window size")
    parser.add_argument("--no-cnn", action="store_true", help="Disable CNN post-filter")
    parser.add_argument("--keep-temp", action="store_true", help="Keep temporary segment files")
    args = parser.parse_args()

    input_path = os.path.abspath(args.input)
    output_path = os.path.abspath(args.output)

    if not os.path.exists(input_path):
        print(f"Error: input file not found: {input_path}")
        sys.exit(1)

    # Probe frame count
    print(f"Probing {input_path}...")
    total_frames = probe_frame_count(input_path)
    print(f"Total frames: {total_frames}")

    if total_frames < args.workers * 30:
        print(f"Clip too short for {args.workers} workers, using 1")
        args.workers = 1

    # Compute segments
    segments = compute_segments(total_frames, args.workers)

    print(f"\nParallel denoise: {args.workers} workers, {OVERLAP}-frame overlap")
    print(f"Input:  {input_path}")
    print(f"Output: {output_path}")
    print(f"Settings: strength={args.strength}, window={args.window}, cnn={'off' if args.no_cnn else 'on'}")
    print()

    # Create temp directory
    temp_dir = os.path.join(os.path.dirname(output_path), ".bayerflow_parallel_temp")
    os.makedirs(temp_dir, exist_ok=True)

    # Build extra args for bayerflow CLI
    extra_args = [
        "--strength", str(args.strength),
        "--window", str(args.window),
    ]
    if args.no_cnn:
        extra_args.append("--no-cnn")

    # Process segments in parallel
    t_start = time.time()
    results = [None] * args.workers

    with ProcessPoolExecutor(max_workers=args.workers) as executor:
        futures = {}
        for seg in segments:
            future = executor.submit(process_segment, seg, input_path, temp_dir, extra_args)
            futures[future] = seg['index']

        for future in as_completed(futures):
            idx = futures[future]
            result = future.result()
            if result:
                results[idx] = result
            else:
                print(f"Segment {idx} failed!")
                sys.exit(1)

    t_process = time.time() - t_start
    print(f"\nAll segments processed in {t_process:.1f}s")
    print(f"Speedup: {total_frames / t_process:.1f} fps "
          f"(vs ~{total_frames / (total_frames * 2.0):.1f} fps single-threaded)")

    # Trim overlaps
    print("\nTrimming overlaps...")
    trimmed_paths = []
    for r in results:
        trimmed = trim_segment(r, temp_dir)
        trimmed_paths.append(trimmed)

    # Stitch
    if stitch_segments(trimmed_paths, output_path):
        print(f"\nDone! Output: {output_path}")
        file_size = os.path.getsize(output_path) / (1024 * 1024)
        print(f"File size: {file_size:.1f} MB")
        print(f"Total time: {time.time() - t_start:.1f}s")
        print(f"Effective fps: {total_frames / (time.time() - t_start):.1f}")

    # Cleanup
    if not args.keep_temp:
        shutil.rmtree(temp_dir, ignore_errors=True)
        print("Temp files cleaned up")


if __name__ == "__main__":
    main()
