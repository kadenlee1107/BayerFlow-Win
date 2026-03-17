#!/usr/bin/env python3
"""
BayerFlow Parallel Denoiser
Each segment gets its own RAFT server + TEMP prefix to avoid conflicts.
True parallel processing with N independent pipelines.
"""

import subprocess, os, sys, time, shutil, threading

BAYERFLOW_EXE = r"C:\Users\kaden\BayerFlow-Win\build\bayerflow.exe"
RAFT_SERVER = r"C:\Users\kaden\BayerFlow-Win\raft_server.py"


def process_segment(idx, input_path, seg_out, start, end, temp_dir):
    """Run one segment with its own RAFT server and TEMP prefix."""
    seg_temp = os.path.join(temp_dir, f"worker_{idx}")
    os.makedirs(seg_temp, exist_ok=True)

    # Set unique TEMP dir so RAFT file IPC doesn't conflict
    env = os.environ.copy()
    env["TEMP"] = seg_temp
    env["TMP"] = seg_temp

    # Start dedicated RAFT server for this segment
    raft_log = open(os.path.join(temp_dir, f"raft_{idx}.log"), 'w')
    raft = subprocess.Popen(["python", RAFT_SERVER], stdout=raft_log, stderr=raft_log, env=env)
    time.sleep(6)  # Wait for model load

    # Run bayerflow
    bf_log = open(os.path.join(temp_dir, f"seg_{idx:03d}.log"), 'w')
    t0 = time.time()
    p = subprocess.Popen(
        [BAYERFLOW_EXE, "--input", input_path, "--output", seg_out,
         "--start", str(start), "--end", str(end)],
        stdout=bf_log, stderr=bf_log, env=env)
    p.wait()
    elapsed = time.time() - t0
    bf_log.close()

    # Kill RAFT server
    raft.kill()
    raft_log.close()

    size = os.path.getsize(seg_out) if os.path.exists(seg_out) else 0
    fps = (end - start) / elapsed if elapsed > 0 else 0
    status = "OK" if p.returncode == 0 and size > 0 else "FAILED"
    print(f"  Segment {idx}: {status} — frames {start}-{end}, {elapsed:.0f}s, {size/1e6:.0f}MB, {fps:.1f}fps")
    return p.returncode == 0 and size > 0


def main():
    if len(sys.argv) < 3:
        print("Usage: parallel_denoise.py input.mov output.mov [workers]")
        sys.exit(1)

    input_path = os.path.abspath(sys.argv[1])
    output_path = os.path.abspath(sys.argv[2])
    workers = int(sys.argv[3]) if len(sys.argv) > 3 else 2

    # Probe frame count
    try:
        r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
            "-count_frames", "-show_entries", "stream=nb_read_frames",
            "-of", "csv=p=0", input_path], capture_output=True, text=True, timeout=120)
        total = int(r.stdout.strip())
    except:
        total = 495

    print(f"BayerFlow Parallel Denoiser")
    print(f"  Input:   {input_path}")
    print(f"  Output:  {output_path}")
    print(f"  Frames:  {total}")
    print(f"  Workers: {workers}")
    print(f"  Each worker gets its own RAFT server + TEMP dir")

    temp_dir = output_path + "_temp"
    os.makedirs(temp_dir, exist_ok=True)

    # Compute segments
    chunk = total // workers
    segments = []
    for i in range(workers):
        start = i * chunk
        end = (i + 1) * chunk if i < workers - 1 else total
        seg_out = os.path.join(temp_dir, f"seg_{i:03d}.mov")
        segments.append((i, start, end, seg_out))
        print(f"  [{i}] frames {start}-{end} ({end-start} frames)")

    # Launch all segments in parallel threads
    print(f"\nLaunching {workers} parallel pipelines...")
    t0 = time.time()
    results = [None] * workers
    threads = []

    def run_seg(idx, start, end, seg_out):
        results[idx] = process_segment(idx, input_path, seg_out, start, end, temp_dir)

    for i, start, end, seg_out in segments:
        t = threading.Thread(target=run_seg, args=(i, start, end, seg_out))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    elapsed = time.time() - t0
    print(f"\nAll segments: {elapsed:.0f}s ({total/elapsed:.1f} fps effective)")

    # Check results
    for i, ok in enumerate(results):
        if not ok:
            print(f"Segment {i} failed! Check logs in {temp_dir}")
            sys.exit(1)

    # Stitch
    concat = os.path.join(temp_dir, "concat.txt")
    with open(concat, 'w') as f:
        for _, _, _, seg_out in segments:
            f.write(f"file '{seg_out}'\n")

    print("Stitching...")
    r = subprocess.run(["ffmpeg", "-y", "-f", "concat", "-safe", "0",
        "-i", concat, "-c", "copy", output_path],
        capture_output=True, text=True)

    if r.returncode != 0:
        print(f"Stitch failed: {r.stderr[:300]}")
        sys.exit(1)

    total_time = time.time() - t0
    size = os.path.getsize(output_path) / (1024*1024) if os.path.exists(output_path) else 0
    print(f"\nDone! {output_path}")
    print(f"  Size: {size:.0f} MB")
    print(f"  Total: {total_time:.0f}s")
    print(f"  Speed: {total/total_time:.1f} fps")

    shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
