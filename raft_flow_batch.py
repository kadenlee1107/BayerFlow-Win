"""RAFT batch optical flow — processes ALL neighbor pairs in one call.
Called: python raft_flow_batch.py <center.raw> <width> <height> <n1.raw> <fx1.raw> <fy1.raw> [<n2.raw> <fx2.raw> <fy2.raw> ...]"""
import sys, os, time
import numpy as np
import torch
import torch.nn.functional as F
import torchvision.models.optical_flow as of_models

def main():
    if len(sys.argv) < 7 or (len(sys.argv) - 4) % 3 != 0:
        print(f"Usage: {sys.argv[0]} <center.raw> <width> <height> [<neighbor.raw> <fx.raw> <fy.raw>] ...", flush=True)
        sys.exit(1)

    center_path = sys.argv[1]
    width = int(sys.argv[2])
    height = int(sys.argv[3])

    # Parse neighbor triplets
    pairs = []
    i = 4
    while i + 2 < len(sys.argv):
        pairs.append((sys.argv[i], sys.argv[i+1], sys.argv[i+2]))
        i += 3

    t0 = time.time()

    # Load model ONCE
    model = of_models.raft_small(weights=of_models.Raft_Small_Weights.DEFAULT)
    model.eval()
    model.cuda()

    # Read center
    center = np.fromfile(center_path, dtype=np.uint16).reshape(height, width)
    scale = 1.0 / 16383.0
    c_t = torch.from_numpy(center.astype(np.float32) * scale).cuda()
    c_3 = c_t.unsqueeze(0).unsqueeze(0).expand(1, 3, -1, -1)

    # Resize for RAFT (half res, mult of 8)
    rh = (height // 2 + 7) & ~7
    rw = (width // 2 + 7) & ~7
    c_r = F.interpolate(c_3, size=(rh, rw), mode='bilinear', align_corners=False)

    t_load = time.time() - t0

    # Process each pair
    for n_path, fx_path, fy_path in pairs:
        t1 = time.time()

        neighbor = np.fromfile(n_path, dtype=np.uint16).reshape(height, width)
        n_t = torch.from_numpy(neighbor.astype(np.float32) * scale).cuda()
        n_3 = n_t.unsqueeze(0).unsqueeze(0).expand(1, 3, -1, -1)
        n_r = F.interpolate(n_3, size=(rh, rw), mode='bilinear', align_corners=False)

        with torch.no_grad():
            flows = model(c_r, n_r)
            flow = flows[-1]

        # Upscale flow
        flow_up = F.interpolate(flow, size=(height, width), mode='bilinear', align_corners=False)
        flow_up[:, 0] *= (width / rw)
        flow_up[:, 1] *= (height / rh)

        fx = flow_up[0, 0].cpu().numpy()
        fy = flow_up[0, 1].cpu().numpy()

        fx.astype(np.float32).tofile(fx_path)
        fy.astype(np.float32).tofile(fy_path)

        mag = np.sqrt(fx*fx + fy*fy)
        dt = time.time() - t1
        print(f"RAFT: mean={mag.mean():.1f}px center=({fx[height//2,width//2]:.2f},{fy[height//2,width//2]:.2f}) [{dt:.2f}s]", flush=True)

    print(f"RAFT batch: {len(pairs)} pairs, load={t_load:.1f}s, total={time.time()-t0:.1f}s", flush=True)

if __name__ == "__main__":
    main()
