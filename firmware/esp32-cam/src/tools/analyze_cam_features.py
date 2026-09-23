#!/usr/bin/env python3

import cv2
import numpy as np
from collections import deque

IMG = "/tmp/cam_original.jpg"

THRESHOLDS = {
    "A": (85, 130, 135, 175),
    "B": (90, 125, 140, 170),
    "C": (82, 128, 138, 176),
    "D": (88, 132, 132, 172),
}

MIN_AREA = 20


def make_mask(img, cb_min, cb_max, cr_min, cr_max):
    ycrcb = cv2.cvtColor(img, cv2.COLOR_BGR2YCrCb)

    Y = ycrcb[:, :, 0]
    Cr = ycrcb[:, :, 1]
    Cb = ycrcb[:, :, 2]

    mask = (
        (Cb >= cb_min) &
        (Cb <= cb_max) &
        (Cr >= cr_min) &
        (Cr <= cr_max)
    ).astype(np.uint8) * 255

    kernel = np.ones((3, 3), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)

    return mask, Y, Cb, Cr


def find_components(mask):
    h, w = mask.shape
    visited = np.zeros_like(mask, dtype=np.uint8)

    result = []

    for y0 in range(h):
        for x0 in range(w):

            if mask[y0, x0] == 0 or visited[y0, x0]:
                continue

            q = deque([(x0, y0)])
            visited[y0, x0] = 1

            pixels = []

            min_x = max_x = x0
            min_y = max_y = y0

            while q:

                x, y = q.popleft()

                pixels.append((x, y))

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

            if len(pixels) < MIN_AREA:
                continue

            result.append({
                "pixels": pixels,
                "x": min_x,
                "y": min_y,
                "w": max_x - min_x + 1,
                "h": max_y - min_y + 1,
                "area": len(pixels),
            })

    result.sort(key=lambda x: x["area"], reverse=True)

    return result


def region_stats(c, Y, Cb, Cr, mask):

    x = c["x"]
    y = c["y"]
    w = c["w"]
    h = c["h"]

    pixels = c["pixels"]

    ys = np.array([p[1] for p in pixels])
    xs = np.array([p[0] for p in pixels])

    yv = Y[ys, xs].astype(np.float32)
    cbv = Cb[ys, xs].astype(np.float32)
    crv = Cr[ys, xs].astype(np.float32)

    # -------------------------------------------------------
    # 基础统计
    # -------------------------------------------------------

    stats = {}

    stats["Y_mean"] = np.mean(yv)
    stats["Y_std"] = np.std(yv)
    stats["Y_min"] = np.min(yv)
    stats["Y_max"] = np.max(yv)

    stats["Cb_mean"] = np.mean(cbv)
    stats["Cb_std"] = np.std(cbv)
    stats["Cb_min"] = np.min(cbv)
    stats["Cb_max"] = np.max(cbv)

    stats["Cr_mean"] = np.mean(crv)
    stats["Cr_std"] = np.std(crv)
    stats["Cr_min"] = np.min(crv)
    stats["Cr_max"] = np.max(crv)

    # -------------------------------------------------------
    # 几何
    # -------------------------------------------------------

    box_area = w * h

    stats["fill"] = c["area"] / box_area

    stats["aspect"] = w / h

    stats["cx"] = (x + w / 2) / 160
    stats["cy"] = (y + h / 2) / 120

    stats["area_ratio"] = c["area"] / (160 * 120)

    # -------------------------------------------------------
    # 边缘密度
    # -------------------------------------------------------

    gray = cv2.cvtColor(
        cv2.imread(IMG),
        cv2.COLOR_BGR2GRAY
    )

    edges = cv2.Canny(gray, 50, 120)

    region_edges = edges[y:y+h, x:x+w]

    if region_edges.size:
        stats["edge_ratio"] = (
            np.count_nonzero(region_edges) /
            region_edges.size
        )
    else:
        stats["edge_ratio"] = 0

    # -------------------------------------------------------
    # 上下左右颜色差异
    # -------------------------------------------------------

    mid_y = y + h // 2
    mid_x = x + w // 2

    top_mask = (ys < mid_y)
    bottom_mask = (ys >= mid_y)

    left_mask = (xs < mid_x)
    right_mask = (xs >= mid_x)

    if np.any(top_mask) and np.any(bottom_mask):
        stats["Y_tb"] = abs(
            np.mean(yv[top_mask]) -
            np.mean(yv[bottom_mask])
        )
    else:
        stats["Y_tb"] = 0

    if np.any(left_mask) and np.any(right_mask):
        stats["Y_lr"] = abs(
            np.mean(yv[left_mask]) -
            np.mean(yv[right_mask])
        )
    else:
        stats["Y_lr"] = 0

    # -------------------------------------------------------
    # 区域边缘接触
    # -------------------------------------------------------

    border_pixels = []

    for px, py in pixels:

        if px == x or px == x + w - 1:
            border_pixels.append((px, py))

        elif py == y or py == y + h - 1:
            border_pixels.append((px, py))

    stats["border_ratio"] = (
        len(border_pixels) / c["area"]
    )

    return stats


def print_stats(name, idx, c, s):

    print()
    print("-" * 90)

    print(
        f"{name} ID={idx}"
    )

    print(
        f"bbox     : "
        f"x={c['x']} y={c['y']} "
        f"w={c['w']} h={c['h']}"
    )

    print(
        f"area     : {c['area']} "
        f"({s['area_ratio']*100:.2f}%)"
    )

    print(
        f"geometry : "
        f"fill={s['fill']:.3f} "
        f"aspect={s['aspect']:.3f} "
        f"center=({s['cx']:.3f},{s['cy']:.3f})"
    )

    print(
        f"Y        : "
        f"mean={s['Y_mean']:.1f} "
        f"std={s['Y_std']:.1f} "
        f"range={s['Y_min']:.0f}-{s['Y_max']:.0f}"
    )

    print(
        f"Cb       : "
        f"mean={s['Cb_mean']:.1f} "
        f"std={s['Cb_std']:.1f} "
        f"range={s['Cb_min']:.0f}-{s['Cb_max']:.0f}"
    )

    print(
        f"Cr       : "
        f"mean={s['Cr_mean']:.1f} "
        f"std={s['Cr_std']:.1f} "
        f"range={s['Cr_min']:.0f}-{s['Cr_max']:.0f}"
    )

    print(
        f"texture  : "
        f"edge={s['edge_ratio']:.3f}"
    )

    print(
        f"contrast : "
        f"Y_top-bottom={s['Y_tb']:.1f} "
        f"Y_left-right={s['Y_lr']:.1f}"
    )

    print(
        f"border   : "
        f"{s['border_ratio']:.3f}"
    )


def main():

    img = cv2.imread(IMG)

    if img is None:
        print("ERROR:", IMG)
        return

    print("=" * 90)
    print("ESP32-CAM REGION FEATURE ANALYSIS")
    print("=" * 90)

    print(f"image: {IMG}")
    print(f"size : {img.shape[1]}x{img.shape[0]}")

    for name, (cb1, cb2, cr1, cr2) in THRESHOLDS.items():

        print()
        print("=" * 90)

        print(
            f"THRESHOLD {name} "
            f"Cb={cb1}-{cb2} "
            f"Cr={cr1}-{cr2}"
        )

        print("=" * 90)

        mask, Y, Cb, Cr = make_mask(
            img,
            cb1,
            cb2,
            cr1,
            cr2
        )

        comps = find_components(mask)

        print(
            f"components: {len(comps)}"
        )

        for i, c in enumerate(comps[:12], 1):

            stats = region_stats(
                c,
                Y,
                Cb,
                Cr,
                mask
            )

            print_stats(
                name,
                i,
                c,
                stats
            )


if __name__ == "__main__":
    main()