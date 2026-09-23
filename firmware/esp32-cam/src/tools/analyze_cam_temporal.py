#!/usr/bin/env python3

import cv2
import numpy as np
import urllib.request
import time
from collections import deque

URL = "http://192.168.0.6/capture"

THRESHOLDS = {
    "A": (85, 130, 135, 175),
    "B": (90, 125, 140, 170),
    "C": (82, 128, 138, 176),
    "D": (88, 132, 132, 172),
}

MIN_AREA = 20


def capture():

    try:
        data = urllib.request.urlopen(
            URL,
            timeout=3
        ).read()

        img = cv2.imdecode(
            np.frombuffer(data, np.uint8),
            cv2.IMREAD_COLOR
        )

        return img

    except Exception as e:

        print("capture error:", e)

        return None


def make_mask(img, threshold):

    cb_min, cb_max, cr_min, cr_max = threshold

    ycrcb = cv2.cvtColor(
        img,
        cv2.COLOR_BGR2YCrCb
    )

    Cr = ycrcb[:, :, 1]
    Cb = ycrcb[:, :, 2]

    mask = (
        (Cb >= cb_min) &
        (Cb <= cb_max) &
        (Cr >= cr_min) &
        (Cr <= cr_max)
    ).astype(np.uint8) * 255

    kernel = np.ones((3, 3), np.uint8)

    mask = cv2.morphologyEx(
        mask,
        cv2.MORPH_OPEN,
        kernel
    )

    return mask


def find_components(mask):

    h, w = mask.shape

    visited = np.zeros_like(
        mask,
        dtype=np.uint8
    )

    result = []

    for y0 in range(h):

        for x0 in range(w):

            if mask[y0, x0] == 0:
                continue

            if visited[y0, x0]:
                continue

            q = deque([(x0, y0)])

            visited[y0, x0] = 1

            min_x = max_x = x0
            min_y = max_y = y0
            area = 0

            while q:

                x, y = q.popleft()

                area += 1

                min_x = min(min_x, x)
                max_x = max(max_x, x)

                min_y = min(min_y, y)
                max_y = max(max_y, y)

                for dy in (-1, 0, 1):

                    for dx in (-1, 0, 1):

                        if dx == 0 and dy == 0:
                            continue

                        nx = x + dx
                        ny = y + dy

                        if nx < 0 or nx >= w:
                            continue

                        if ny < 0 or ny >= h:
                            continue

                        if visited[ny, nx]:
                            continue

                        if mask[ny, nx] == 0:
                            continue

                        visited[ny, nx] = 1

                        q.append((nx, ny))

            if area < MIN_AREA:
                continue

            bw = max_x - min_x + 1
            bh = max_y - min_y + 1

            fill = area / (bw * bh)
            aspect = bw / bh

            result.append({
                "x": min_x,
                "y": min_y,
                "w": bw,
                "h": bh,
                "area": area,
                "fill": fill,
                "aspect": aspect,
            })

    result.sort(
        key=lambda x: x["area"],
        reverse=True
    )

    return result


def center(c):

    return (
        c["x"] + c["w"] / 2,
        c["y"] + c["h"] / 2
    )


def distance(a, b):

    ax, ay = center(a)
    bx, by = center(b)

    return (
        (ax - bx) ** 2 +
        (ay - by) ** 2
    ) ** 0.5


def main():

    history = {
        name: []
        for name in THRESHOLDS
    }

    print("=" * 90)
    print("ESP32-CAM TEMPORAL REGION ANALYSIS")
    print("=" * 90)

    print("Capturing 20 frames...")
    print()

    for frame_no in range(20):

        img = capture()

        if img is None:

            print(
                f"frame {frame_no+1:02d}: FAILED"
            )

            time.sleep(0.5)

            continue

        print(
            f"FRAME {frame_no+1:02d}"
        )

        for name, threshold in THRESHOLDS.items():

            mask = make_mask(
                img,
                threshold
            )

            comps = find_components(mask)

            if not comps:

                print(
                    f"  {name}: no components"
                )

                continue

            c = comps[0]

            history[name].append(c)

            print(
                f"  {name}: "
                f"x={c['x']:3d} "
                f"y={c['y']:3d} "
                f"w={c['w']:3d} "
                f"h={c['h']:3d} "
                f"area={c['area']:4d} "
                f"fill={c['fill']:.2f} "
                f"asp={c['aspect']:.2f}"
            )

        print()

        time.sleep(0.3)

    print()
    print("=" * 90)
    print("TEMPORAL SUMMARY")
    print("=" * 90)

    for name, items in history.items():

        if not items:

            print(
                f"{name}: no valid regions"
            )

            continue

        xs = np.array([
            center(c)[0]
            for c in items
        ])

        ys = np.array([
            center(c)[1]
            for c in items
        ])

        ws = np.array([
            c["w"]
            for c in items
        ])

        hs = np.array([
            c["h"]
            for c in items
        ])

        areas = np.array([
            c["area"]
            for c in items
        ])

        print()
        print(f"THRESHOLD {name}")

        print(
            f"frames     : {len(items)}/20"
        )

        print(
            f"center X   : "
            f"mean={xs.mean():.1f} "
            f"std={xs.std():.1f} "
            f"range={xs.min():.1f}-{xs.max():.1f}"
        )

        print(
            f"center Y   : "
            f"mean={ys.mean():.1f} "
            f"std={ys.std():.1f} "
            f"range={ys.min():.1f}-{ys.max():.1f}"
        )

        print(
            f"width      : "
            f"mean={ws.mean():.1f} "
            f"std={ws.std():.1f}"
        )

        print(
            f"height     : "
            f"mean={hs.mean():.1f} "
            f"std={hs.std():.1f}"
        )

        print(
            f"area       : "
            f"mean={areas.mean():.1f} "
            f"std={areas.std():.1f}"
        )


if __name__ == "__main__":
    main()