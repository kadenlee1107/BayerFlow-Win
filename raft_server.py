"""Persistent RAFT server v5 — tiled full-res with memory leak fix.
Added torch.cuda.empty_cache() and explicit tensor deletion."""
import os, sys, time
import numpy as np
import torch
import torch.nn.functional as F
import torchvision.models.optical_flow as of_models

OVERLAP = 64

def main():
    tmp = os.environ.get('TEMP', '.')
    cmd_file = os.path.join(tmp, 'bf_raft_cmd.txt')
    go_file = os.path.join(tmp, 'bf_raft_go')
    done_file = os.path.join(tmp, 'bf_raft_done')

    print("RAFT server v5: loading model...", flush=True)
    model = of_models.raft_small(weights=of_models.Raft_Small_Weights.DEFAULT)
    model.eval()
    model.cuda()
    print("RAFT server v5: ready (tiled, leak-fixed)", flush=True)

    for f in [go_file, done_file]:
        try: os.remove(f)
        except: pass

    count = 0
    while True:
        if not os.path.exists(go_file):
            time.sleep(0.01)
            continue

        for _ in range(10):
            try: os.remove(go_file); break
            except: time.sleep(0.01)

        t0 = time.time()
        try:
            with open(cmd_file, 'r') as f:
                parts = f.read().strip().split()

            center_path = parts[0]
            width, height = int(parts[1]), int(parts[2])

            pairs = []
            i = 3
            while i + 2 < len(parts):
                pairs.append((parts[i], parts[i+1], parts[i+2]))
                i += 3

            center = np.fromfile(center_path, dtype=np.uint16).reshape(height, width)
            scale = 1.0 / 16383.0
            c_f = torch.from_numpy(center.astype(np.float32) * scale).cuda()

            mid_y = height // 2
            mid_x = width // 2
            tiles = [
                (0, 0, mid_x + OVERLAP, mid_y + OVERLAP),
                (mid_x - OVERLAP, 0, width, mid_y + OVERLAP),
                (0, mid_y - OVERLAP, mid_x + OVERLAP, height),
                (mid_x - OVERLAP, mid_y - OVERLAP, width, height),
            ]

            for n_path, fx_path, fy_path in pairs:
                nb = np.fromfile(n_path, dtype=np.uint16).reshape(height, width)
                n_f = torch.from_numpy(nb.astype(np.float32) * scale).cuda()

                fx_full = torch.zeros(height, width, device='cuda')
                fy_full = torch.zeros(height, width, device='cuda')
                weight = torch.zeros(height, width, device='cuda')

                for (x0, y0, x1, y1) in tiles:
                    th = y1 - y0
                    tw = x1 - x0
                    ph = (8 - th % 8) % 8
                    pw = (8 - tw % 8) % 8

                    c_tile = c_f[y0:y1, x0:x1].unsqueeze(0).unsqueeze(0).expand(1, 3, th, tw).clone()
                    n_tile = n_f[y0:y1, x0:x1].unsqueeze(0).unsqueeze(0).expand(1, 3, th, tw).clone()

                    if ph > 0 or pw > 0:
                        c_tile = F.pad(c_tile, (0, pw, 0, ph), mode='reflect')
                        n_tile = F.pad(n_tile, (0, pw, 0, ph), mode='reflect')

                    with torch.no_grad():
                        flow = model(c_tile, n_tile)[-1]

                    flow = flow[:, :, :th, :tw]

                    # Blend weight
                    w = torch.ones(th, tw, device='cuda')
                    if x0 > 0:
                        ramp = torch.linspace(0, 1, min(OVERLAP, tw), device='cuda')
                        w[:, :len(ramp)] *= ramp
                    if x1 < width:
                        ramp = torch.linspace(1, 0, min(OVERLAP, tw), device='cuda')
                        w[:, -len(ramp):] *= ramp
                    if y0 > 0:
                        ramp = torch.linspace(0, 1, min(OVERLAP, th), device='cuda').unsqueeze(1)
                        w[:len(ramp.squeeze()), :] *= ramp
                    if y1 < height:
                        ramp = torch.linspace(1, 0, min(OVERLAP, th), device='cuda').unsqueeze(1)
                        w[-len(ramp.squeeze()):, :] *= ramp

                    fx_full[y0:y1, x0:x1] += flow[0, 0] * w
                    fy_full[y0:y1, x0:x1] += flow[0, 1] * w
                    weight[y0:y1, x0:x1] += w

                    # Free tile tensors immediately
                    del c_tile, n_tile, flow, w

                mask = weight > 0
                fx_full[mask] /= weight[mask]
                fy_full[mask] /= weight[mask]

                fx_full.cpu().numpy().astype(np.float32).tofile(fx_path)
                fy_full.cpu().numpy().astype(np.float32).tofile(fy_path)

                # Free all per-pair tensors
                del n_f, fx_full, fy_full, weight, mask
                torch.cuda.empty_cache()

            # Free center tensor
            del c_f

            # Periodic GPU cache clear
            count += 1
            if count % 10 == 0:
                torch.cuda.empty_cache()

            dt = time.time() - t0
            print(f"RAFT #{count}: {len(pairs)}p {dt:.2f}s ({dt/max(len(pairs),1):.2f}s/p)", flush=True)

        except Exception as e:
            import traceback
            print(f"RAFT error: {e}", flush=True)
            traceback.print_exc()

        with open(done_file, 'w') as f:
            f.write('done')

if __name__ == "__main__":
    main()
