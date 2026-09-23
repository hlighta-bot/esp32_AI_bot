#!/usr/bin/env python3

import cv2
import numpy as np
from collections import deque

IMG = "/tmp/cam_original.jpg"

# ============================================================
# Threshold sets
# ============================================================

THRESHOLDS = {
    "A": (85, 130, 135, 175),
    "B": (90, 125, 140, 170),
    "C": (82, 128, 138, 176),
    "D": (88, 132, 132, 172),
}

# ESP32 当前过滤条件
MIN_AREA = 20
MAX_AREA_RATIO = 0.60
MIN_W = 4
MIN_H = 4
MIN_FILL = 0.20
MAX_FILL = 0.95
MIN_ASPECT = 0.20
MAX_ASPECT = 1.20


def make_mask(img, cb_min, cb_max, cr_min, cr_max):
    ycrcb = cv2.cvtColor(img, cv2.COLOR_BGR2YCrCb)

    # OpenCV: Y, Cr, Cb
    cr = ycrcb[:, :, 1]
    cb = ycrcb[:, :, 2]

    mask = (
        (cb >= cb_min) &
        (cb <= cb_max) &
        (cr >= cr_min) &
        (cr <= cr_max)
    ).astype(np.uint8) * 255

    # 与 ESP32 目前 3x3 opening 对齐
    kernel = np.ones((3, 3), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)

    return mask


def components(mask):
    h, w = mask.shape
    visited = np.zeros_like(mask, dtype=np.uint8)

    result = []

    for y0 in range(h):
        for x0 in range(w):
            if mask[y0, x0] == 0 or visited[y0, x0]:
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

                        if nx < 0 or nx >= w or ny < 0 or ny >= h:
                            continue

                        if visited[ny, nx]:
                            continue

                        if mask[ny, nx] == 0:
                            continue

                        visited[ny, nx] = 1
                        q.append((nx, ny))

            bw = max_x - min_x + 1
            bh = max_y - min_y + 1

            box_area = bw * bh
            fill = area / box_area if box_area else 0
            aspect = bw / bh if bh else 0

            cx = (min_x + max_x) / 2
            cy = (min_y + max_y) / 2

            result.append({
                "x": min_x,
                "y": min_y,
                "w": bw,
                "h": bh,
                "area": area,
                "fill": fill,
                "aspect": aspect,
                "cx": cx,
                "cy": cy,
            })

    return sorted(result, key=lambda c: c["area"], reverse=True)


def reason(c, total):
    reasons = []

    if c["area"] < MIN_AREA:
        reasons.append("area<20")

    if c["area"] > total * MAX_AREA_RATIO:
        reasons.append("area>60%")

    if c["w"] < MIN_W:
        reasons.append("w<4")

    if c["h"] < MIN_H:
        reasons.append("h<4")

    if c["fill"] < MIN_FILL:
        reasons.append("fill<.20")

    if c["fill"] > MAX_FILL:
        reasons.append("fill>.95")

    if c["aspect"] < MIN_ASPECT:
        reasons.append("aspect<.20")

    if c["aspect"] > MAX_ASPECT:
        reasons.append("aspect>1.20")

    return "PASS" if not reasons else "REJECT:" + ",".join(reasons)


def print_component(i, c, total):
    x = c["x"]
    y = c["y"]
    w = c["w"]
    h = c["h"]
    area = c["area"]

    print(
        f"{i:2d} "
        f"x={x:3d} y={y:3d} "
        f"w={w:3d} h={h:3d} "
        f"area={area:5d} "
        f"area%={area/total*100:6.2f} "
        f"fill={c['fill']:.3f} "
        f"aspect={c['aspect']:.3f} "
        f"center=({c['cx']/160:.3f},{c['cy']/120:.3f}) "
        f"{reason(c,total)}"
    )


def main():

    img = cv2.imread(IMG)

    if img is None:
        print("ERROR: cannot read", IMG)
        return

    h, w = img.shape[:2]
    total = w * h

    print("=" * 90)
    print("ESP32-CAM REGION DIAGNOSTIC")
    print("=" * 90)
    print(f"image size: {w}x{h}")
    print()

    for name, (cb_min, cb_max, cr_min, cr_max) in THRESHOLDS.items():

        print()
        print("=" * 90)
        print(
            f"THRESHOLD {name}: "
            f"Cb={cb_min}-{cb_max}, "
            f"Cr={cr_min}-{cr_max}"
        )
        print("=" * 90)

        mask = make_mask(
            img,
            cb_min,
            cb_max,
            cr_min,
            cr_max
        )

        skin = int(np.count_nonzero(mask))
        comps = components(mask)

        print(
            f"skin pixels={skin} "
            f"skin ratio={skin/total*100:.2f}% "
            f"components={len(comps)}"
        )

        print()
        print(
            " ID "
            " x   y   w   h "
            " area   area% "
            " fill  aspect "
            " center"
        )

        for i, c in enumerate(comps[:20], 1):
            print_component(i, c, total)

        # ----------------------------------------------------
        # 输出候选框图
        # ----------------------------------------------------

        out = img.copy()

        for i, c in enumerate(comps[:20], 1):

            x = c["x"]
            y = c["y"]
            cw = c["w"]
            ch = c["h"]

            passed = reason(c, total) == "PASS"

            if passed:
                thickness = 2
            else:
                thickness = 1

            # 不指定颜色：
            # PASS 使用白色，REJECT 使用灰色
            color = (255, 255, 255) if passed else (128, 128, 128)

            cv2.rectangle(
                out,
                (x, y),
                (x + cw - 1, y + ch - 1),
                color,
                thickness
            )

            cv2.putText(
                out,
                str(i),
                (x, max(10, y + 10)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.35,
                color,
                1,
                cv2.LINE_AA
            )

        filename = f"/tmp/cam_regions_{name.lower()}.jpg"
        cv2.imwrite(filename, out)

        mask_filename = f"/tmp/cam_regions_{name.lower()}_mask.jpg"
        cv2.imwrite(mask_filename, mask)

        print()
        print(f"saved: {filename}")
        print(f"saved: {mask_filename}")


if __name__ == "__main__":
    main()