# Measures camera yaw between two screenshots via phase correlation on the
# horizon band. Usage: measure_yaw.py a.png b.png [focal_px_at_capture_width]
import sys, math
import numpy as np
from PIL import Image

def load(p):
    im = Image.open(p).convert("L")
    w, h = im.size
    band = im.crop((0, int(h * 0.30), w, int(h * 0.55)))
    return np.asarray(band, dtype=np.float32), w

a, w = load(sys.argv[1])
b, _ = load(sys.argv[2])
focal = float(sys.argv[3]) if len(sys.argv) > 3 else 2639.0 * w / 3840.0
win = np.hanning(a.shape[1])[None, :] * np.hanning(a.shape[0])[:, None]
A = np.fft.fft2((a - a.mean()) * win)
B = np.fft.fft2((b - b.mean()) * win)
R = A * np.conj(B)
R /= np.abs(R) + 1e-9
r = np.abs(np.fft.ifft2(R))
dy, dx = np.unravel_index(np.argmax(r), r.shape)
if dx > a.shape[1] // 2: dx -= a.shape[1]
if dy > a.shape[0] // 2: dy -= a.shape[0]
peak = r.max() / r.mean()
yaw = math.degrees(math.atan(dx / focal))
print("shift %d px (dy %d), yaw %.2f deg, peak %.1f" % (dx, dy, yaw, peak))
