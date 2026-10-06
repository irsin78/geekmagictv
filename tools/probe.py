"""Run every safeboot diagnostic against the device and save a JSON report.

Usage: .venv/bin/python tools/probe.py [host]   (or set SMALLTV_HOST)
Needs the safeboot firmware on the device (its /api/bench/* endpoints) and `pip install -r tools/requirements.txt`.
Reports go to local/reports/ (git-ignored).
"""
import io
import json
import os
import sys
import time
from datetime import datetime
from pathlib import Path

import requests
from PIL import Image, ImageDraw

HOST = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("SMALLTV_HOST", "192.168.4.1")
BASE = f"http://{HOST}"
ROOT = Path(__file__).resolve().parent.parent


def get(path, timeout=30):
    r = requests.get(BASE + path, timeout=timeout)
    r.raise_for_status()
    return r.json()


def post(path, data, name):
    t = time.perf_counter()
    r = requests.post(BASE + path, files={"f": (name, data)}, timeout=60)
    r.raise_for_status()
    out = r.json()
    out["wall_ms"] = round((time.perf_counter() - t) * 1000)
    return out


def frame(k):
    img = Image.new("RGB", (240, 240))
    d = ImageDraw.Draw(img)
    for y in range(240):
        d.line([(0, y), (239, y)], fill=((k * 80 + y) % 256, (255 - y) % 256, (k * 40) % 256))
    d.text((90, 110), f"PC frame {k + 1}", fill=(255, 255, 255))
    return img


def rgb565_be(img):
    out = bytearray(240 * 240 * 2)
    for i, (r, g, b) in enumerate(img.getdata()):
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[2 * i], out[2 * i + 1] = v >> 8, v & 0xFF
    return bytes(out)


def jpeg(img, q):
    b = io.BytesIO()
    img.save(b, "JPEG", quality=q)
    return b.getvalue()


def main():
    rep = {"time": datetime.now().isoformat(timespec="seconds")}
    rep["info"] = get("/api/info")
    # CPU clock is a build option (board_build.f_cpu); runtime switching is a no-op.
    rep["cpu"] = get("/api/bench/cpu")
    rep["mem"] = get("/api/bench/mem")
    rep["flash"] = get("/api/bench/flash")

    t = time.perf_counter()
    n = len(requests.get(BASE + "/api/bench/zero?len=1048576", timeout=120).content)
    s = time.perf_counter() - t
    rep["net_download"] = {"bytes": n, "sec": round(s, 2), "KBps": round(n / 1024 / s)}
    rep["net_upload"] = post("/api/bench/sink", bytes(262144), "z.bin")

    rep["wifi_scan"] = get("/api/wifi/scan")
    rep["display_bench"] = get("/api/display/bench", timeout=60)

    frames = [frame(k) for k in range(3)]
    rep["push_raw"] = [post("/api/display/raw", rgb565_be(f), "f.raw") for f in frames]
    for q in (60, 85):
        rep[f"push_jpeg_q{q}"] = [post("/api/display/jpeg", jpeg(f, q), "f.jpg") for f in frames]
    get("/api/display/pattern?n=status")
    rep["info_after"] = get("/api/info")

    out = ROOT / "local" / "reports" / f"probe-{datetime.now():%Y%m%d-%H%M%S}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(rep, indent=2, ensure_ascii=False))
    print(json.dumps(rep, indent=1, ensure_ascii=False))
    print("saved", out)


if __name__ == "__main__":
    main()
