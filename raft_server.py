"""Persistent RAFT optical flow server v2 — fixed race condition."""
import os, sys, time
import numpy as np
import torch
import torch.nn.functional as F
import torchvision.models.optical_flow as of_models

def main():
    tmp = os.environ.get('TEMP', '.')
    cmd_file = os.path.join(tmp, 'bf_raft_cmd.txt')
    go_file = os.path.join(tmp, 'bf_raft_go')
    done_file = os.path.join(tmp, 'bf_raft_done')

    print("RAFT server: loading model...", flush=True)
    model = of_models.raft_small(weights=of_models.Raft_Small_Weights.DEFAULT)
    model.eval()
    model.cuda()
    print("RAFT server: ready", flush=True)

    for f in [go_file, done_file]:
        try: os.remove(f)
        except: pass

    count = 0
    while True:
        if not os.path.exists(go_file):
            time.sleep(0.01)
            continue

        # Safe delete with retry
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
            c_t = torch.from_numpy(center.astype(np.float32) / 16383.0).cuda()
            c_3 = c_t.unsqueeze(0).unsqueeze(0).expand(1, 3, -1, -1)
            rh = (height // 2 + 7) & ~7
            rw = (width // 2 + 7) & ~7
            c_r = F.interpolate(c_3, size=(rh, rw), mode='bilinear', align_corners=False)

            for n_path, fx_path, fy_path in pairs:
                nb = np.fromfile(n_path, dtype=np.uint16).reshape(height, width)
                n_t = torch.from_numpy(nb.astype(np.float32) / 16383.0).cuda()
                n_3 = n_t.unsqueeze(0).unsqueeze(0).expand(1, 3, -1, -1)
                n_r = F.interpolate(n_3, size=(rh, rw), mode='bilinear', align_corners=False)

                with torch.no_grad():
                    flow = model(c_r, n_r)[-1]

                flow_up = F.interpolate(flow, size=(height, width), mode='bilinear', align_corners=False)
                flow_up[:, 0] *= (width / rw)
                flow_up[:, 1] *= (height / rh)

                flow_up[0, 0].cpu().numpy().astype(np.float32).tofile(fx_path)
                flow_up[0, 1].cpu().numpy().astype(np.float32).tofile(fy_path)

            count += 1
            dt = time.time() - t0
            print(f"RAFT #{count}: {len(pairs)}p {dt:.1f}s ({dt/max(len(pairs),1):.2f}s/p)", flush=True)

        except Exception as e:
            print(f"RAFT error: {e}", flush=True)

        with open(done_file, 'w') as f:
            f.write('done')

if __name__ == "__main__":
    main()
