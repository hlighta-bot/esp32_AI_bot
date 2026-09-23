#!/usr/bin/env python3

import argparse
import cv2
import numpy as np


DEFAULT_INPUT = "/tmp/cam_original.jpg"


# ============================================================
# 五组阈值
# ============================================================

THRESHOLDS = [
    ("CURRENT", 77, 135, 128, 180),
    ("A",       85, 130, 135, 175),
    ("B",       90, 125, 140, 170),
    ("C",       82, 128, 138, 176),
    ("D",       88, 132, 132, 172),
]


# ============================================================
# 计算连通区域
# ============================================================

def analyze_components(mask):

    height, width = mask.shape

    total_pixels = width * height

    num_labels, labels, stats, centroids = \
        cv2.connectedComponentsWithStats(
            mask,
            connectivity=8
        )

    components = []

    for label in range(1, num_labels):

        x = int(stats[label, cv2.CC_STAT_LEFT])
        y = int(stats[label, cv2.CC_STAT_TOP])
        w = int(stats[label, cv2.CC_STAT_WIDTH])
        h = int(stats[label, cv2.CC_STAT_HEIGHT])
        area = int(stats[label, cv2.CC_STAT_AREA])

        if w <= 0 or h <= 0:
            continue

        bbox_area = w * h

        fill = area / bbox_area
        aspect = w / h
        area_ratio = area / total_pixels

        cx = float(centroids[label][0])
        cy = float(centroids[label][1])

        components.append({
            "label": label,
            "x": x,
            "y": y,
            "w": w,
            "h": h,
            "area": area,
            "area_ratio": area_ratio,
            "fill": fill,
            "aspect": aspect,
            "cx": cx,
            "cy": cy,
            "cx_norm": cx / width,
            "cy_norm": cy / height,
        })

    # 面积从大到小
    components.sort(
        key=lambda c: c["area"],
        reverse=True
    )

    return components


# ============================================================
# 分析一组阈值
# ============================================================

def analyze_threshold(
    name,
    cb,
    cr,
    cb_min,
    cb_max,
    cr_min,
    cr_max
):

    mask = (
        (cb >= cb_min)
        & (cb <= cb_max)
        & (cr >= cr_min)
        & (cr <= cr_max)
    )

    mask = (
        mask.astype(np.uint8) * 255
    )

    # 与 ESP32 当前算法一致
    kernel = np.ones(
        (3, 3),
        np.uint8
    )

    mask = cv2.morphologyEx(
        mask,
        cv2.MORPH_OPEN,
        kernel
    )

    total_pixels = mask.size

    skin_pixels = int(
        np.count_nonzero(mask)
    )

    skin_ratio = (
        skin_pixels /
        total_pixels *
        100
    )

    components = analyze_components(
        mask
    )

    return {
        "name": name,
        "cb_min": cb_min,
        "cb_max": cb_max,
        "cr_min": cr_min,
        "cr_max": cr_max,
        "mask": mask,
        "skin_pixels": skin_pixels,
        "skin_ratio": skin_ratio,
        "components": components,
    }


# ============================================================
# 输出简表
# ============================================================

def print_summary(results):

    print()
    print("============================================================")
    print("                 THRESHOLD SUMMARY")
    print("============================================================")

    print()

    print(
        "NAME      Cb range     Cr range     "
        "skin%     components   largest"
    )

    print(
        "--------  -----------  -----------  "
        "-------   ----------   -------"
    )

    for r in results:

        components = r["components"]

        if components:
            largest = components[0]["area"]
        else:
            largest = 0

        print(
            f"{r['name']:8s}  "
            f"{r['cb_min']:3d}-{r['cb_max']:3d}      "
            f"{r['cr_min']:3d}-{r['cr_max']:3d}      "
            f"{r['skin_ratio']:6.2f}%   "
            f"{len(components):10d}   "
            f"{largest:7d}"
        )


# ============================================================
# 输出每组最大的几个连通区域
# ============================================================

def print_components(results):

    for r in results:

        print()
        print()
        print("============================================================")
        print(
            f"                  {r['name']}"
        )
        print("============================================================")

        print(
            f"Cb = {r['cb_min']} ~ {r['cb_max']}"
        )

        print(
            f"Cr = {r['cr_min']} ~ {r['cr_max']}"
        )

        print(
            f"skin pixels = {r['skin_pixels']}"
        )

        print(
            f"skin ratio  = {r['skin_ratio']:.2f}%"
        )

        print(
            f"components  = {len(r['components'])}"
        )

        print()

        if not r["components"]:

            print(
                "  No connected components."
            )

            continue

        print(
            "rank  ID    x    y    w    h    "
            "area     area%    fill    aspect   center"
        )

        print(
            "----  ---  ---  ---  ---  ---  "
            "-------  -------  ------  -------  -------------"
        )

        # 只打印最大的 15 个
        for rank, c in enumerate(
            r["components"][:15],
            start=1
        ):

            print(
                f"{rank:4d}  "
                f"{c['label']:3d}  "
                f"{c['x']:3d}  "
                f"{c['y']:3d}  "
                f"{c['w']:3d}  "
                f"{c['h']:3d}  "
                f"{c['area']:7d}  "
                f"{c['area_ratio'] * 100:6.2f}%  "
                f"{c['fill']:6.3f}  "
                f"{c['aspect']:7.3f}  "
                f"({c['cx_norm']:.2f},"
                f"{c['cy_norm']:.2f})"
            )


# ============================================================
# 判断 ESP32 当前候选过滤条件
# ============================================================

def print_filter_result(results):

    print()
    print()
    print("============================================================")
    print("              ESP32 FILTER CHECK")
    print("============================================================")

    # 当前 ESP32 的基础过滤条件
    MIN_AREA = 20
    MAX_AREA_RATIO = 0.60

    MIN_W = 4
    MIN_H = 4

    MIN_FILL = 0.20
    MAX_FILL = 0.95

    MIN_ASPECT = 0.20
    MAX_ASPECT = 1.20

    for r in results:

        print()
        print(
            f"--- {r['name']} ---"
        )

        passed = 0

        for rank, c in enumerate(
            r["components"],
            start=1
        ):

            reasons = []

            if c["area"] < MIN_AREA:
                reasons.append(
                    "area<20"
                )

            if c["area_ratio"] > MAX_AREA_RATIO:
                reasons.append(
                    "area>60%"
                )

            if c["w"] < MIN_W:
                reasons.append(
                    "w<4"
                )

            if c["h"] < MIN_H:
                reasons.append(
                    "h<4"
                )

            if c["fill"] < MIN_FILL:
                reasons.append(
                    "fill<0.20"
                )

            if c["fill"] > MAX_FILL:
                reasons.append(
                    "fill>0.95"
                )

            if c["aspect"] < MIN_ASPECT:
                reasons.append(
                    "aspect<0.20"
                )

            if c["aspect"] > MAX_ASPECT:
                reasons.append(
                    "aspect>1.20"
                )

            if reasons:

                result = "REJECT"

            else:

                result = "PASS"
                passed += 1

            # 只显示最大的 10 个
            if rank <= 10:

                reason = (
                    ", ".join(reasons)
                    if reasons
                    else "-"
                )

                print(
                    f"#{rank:2d} "
                    f"{result:6s} "
                    f"ID={c['label']:3d} "
                    f"bbox="
                    f"({c['x']},{c['y']},"
                    f"{c['w']},{c['h']}) "
                    f"area={c['area']:5d} "
                    f"fill={c['fill']:.3f} "
                    f"aspect={c['aspect']:.3f} "
                    f"reason={reason}"
                )

        print(
            f"PASS components = {passed}"
        )


# ============================================================
# 保存每组 mask
# ============================================================

def save_masks(results):

    for r in results:

        filename = (
            f"/tmp/cam_mask_{r['name'].lower()}.jpg"
        )

        cv2.imwrite(
            filename,
            r["mask"]
        )

        print(
            f"[OUTPUT] {filename}"
        )


# ============================================================
# 生成五组对比图
# ============================================================

def save_comparison(results):

    images = []

    for r in results:

        mask = r["mask"]

        # 转成 BGR
        img = cv2.cvtColor(
            mask,
            cv2.COLOR_GRAY2BGR
        )

        # 放大到 320x240
        img = cv2.resize(
            img,
            (320, 240),
            interpolation=cv2.INTER_NEAREST
        )

        # 写名称
        cv2.putText(
            img,
            r["name"],
            (10, 25),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (0, 0, 255),
            2
        )

        images.append(img)

    # 5 张横向太宽，因此做成：
    #
    # CURRENT A
    # B       C
    # D
    #
    blank = np.zeros_like(images[0])

    row1 = np.hstack(
        [images[0], images[1]]
    )

    row2 = np.hstack(
        [images[2], images[3]]
    )

    row3 = np.hstack(
        [images[4], blank]
    )

    comparison = np.vstack(
        [row1, row2, row3]
    )

    output = "/tmp/cam_threshold_compare.jpg"

    cv2.imwrite(
        output,
        comparison
    )

    print(
        f"[OUTPUT] {output}"
    )


# ============================================================
# Main
# ============================================================

def main():

    parser = argparse.ArgumentParser(
        description=(
            "Compare ESP32-CAM skin thresholds "
            "using connected components"
        )
    )

    parser.add_argument(
        "--input",
        default=DEFAULT_INPUT,
        help="Input image"
    )

    args = parser.parse_args()

    # --------------------------------------------------------
    # 读取图片
    # --------------------------------------------------------

    img = cv2.imread(
        args.input
    )

    if img is None:

        print(
            f"[ERROR] Cannot read: {args.input}"
        )

        return 1

    height, width = img.shape[:2]

    print()
    print("============================================================")
    print("                 CAMERA IMAGE")
    print("============================================================")

    print(
        f"file   : {args.input}"
    )

    print(
        f"size   : {width} x {height}"
    )

    print(
        f"pixels : {width * height}"
    )

    # --------------------------------------------------------
    # BGR -> YCrCb
    # --------------------------------------------------------

    ycrcb = cv2.cvtColor(
        img,
        cv2.COLOR_BGR2YCrCb
    )

    cr = ycrcb[:, :, 1]
    cb = ycrcb[:, :, 2]

    # --------------------------------------------------------
    # 分析所有阈值
    # --------------------------------------------------------

    results = []

    for name, cb_min, cb_max, cr_min, cr_max in THRESHOLDS:

        result = analyze_threshold(
            name,
            cb,
            cr,
            cb_min,
            cb_max,
            cr_min,
            cr_max
        )

        results.append(
            result
        )

    # --------------------------------------------------------
    # 总结
    # --------------------------------------------------------

    print_summary(
        results
    )

    # --------------------------------------------------------
    # 连通区域
    # --------------------------------------------------------

    print_components(
        results
    )

    # --------------------------------------------------------
    # ESP32 filter
    # --------------------------------------------------------

    print_filter_result(
        results
    )

    # --------------------------------------------------------
    # 保存 mask
    # --------------------------------------------------------

    print()
    print(
        "============================================================"
    )
    print(
        "                    OUTPUT"
    )
    print(
        "============================================================"
    )

    save_masks(
        results
    )

    save_comparison(
        results
    )

    print()
    print(
        "========== DONE =========="
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(
        main()
    )