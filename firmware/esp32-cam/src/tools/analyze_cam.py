#!/usr/bin/env python3

import argparse
import urllib.request
import os
import cv2
import numpy as np


DEFAULT_URL = "http://192.168.0.6/capture"

# 与 ESP32 当前 S12-2 保持一致
CB_MIN = 77
CB_MAX = 135
CR_MIN = 128
CR_MAX = 180

# 3x3 morphology opening
KERNEL = np.ones((3, 3), np.uint8)


def download_image(url, output):
    print(f"[HTTP] GET {url}")

    try:
        urllib.request.urlretrieve(url, output)
    except Exception as e:
        print(f"[ERROR] download failed: {e}")
        return False

    print(f"[HTTP] saved: {output}")
    return True


def make_skin_mask(img):
    """
    OpenCV:
        YCrCb channel 0 = Y
        channel 1 = Cr
        channel 2 = Cb
    """

    ycrcb = cv2.cvtColor(img, cv2.COLOR_BGR2YCrCb)

    cr = ycrcb[:, :, 1]
    cb = ycrcb[:, :, 2]

    mask = (
        (cb >= CB_MIN)
        & (cb <= CB_MAX)
        & (cr >= CR_MIN)
        & (cr <= CR_MAX)
    )

    mask = (mask.astype(np.uint8) * 255)

    # 与 ESP32 当前算法类似：3x3 opening
    mask = cv2.morphologyEx(
        mask,
        cv2.MORPH_OPEN,
        KERNEL
    )

    return mask


def analyze_components(mask):
    """
    找出所有连通区域。

    注意：
    这里故意不进行 ESP32 的面积/宽高/fill/aspect 过滤。

    目的：
    先看看到底有哪些区域被判断成 skin。
    """

    num_labels, labels, stats, centroids = cv2.connectedComponentsWithStats(
        mask,
        connectivity=8
    )

    components = []

    height, width = mask.shape
    total_pixels = width * height

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

        cx_norm = cx / width
        cy_norm = cy / height

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
            "cx_norm": cx_norm,
            "cy_norm": cy_norm,
        })

    # 按面积从大到小
    components.sort(
        key=lambda c: c["area"],
        reverse=True
    )

    return components


def print_components(components, image_width, image_height):

    print()
    print("========== ALL COMPONENTS ==========")
    print(f"components : {len(components)}")

    if not components:
        print("No connected components.")
        return

    print()

    print(
        "ID   "
        "x    y    w    h    "
        "area     "
        "area%    "
        "fill     "
        "aspect   "
        "center"
    )

    print(
        "---  "
        "---  ---  ---  ---  "
        "-------  "
        "-------  "
        "-------  "
        "-------  "
        "----------------"
    )

    for i, c in enumerate(components):

        print(
            f"{c['label']:3d}  "
            f"{c['x']:3d}  "
            f"{c['y']:3d}  "
            f"{c['w']:3d}  "
            f"{c['h']:3d}  "
            f"{c['area']:7d}  "
            f"{c['area_ratio'] * 100:6.2f}%  "
            f"{c['fill']:7.3f}  "
            f"{c['aspect']:7.3f}  "
            f"({c['cx_norm']:.2f}, {c['cy_norm']:.2f})"
        )

    print()
    print("字段说明:")
    print("  x,y,w,h    : 连通区域外接矩形")
    print("  area       : skin 像素数量")
    print("  area%      : 占整张图片比例")
    print("  fill       : area / (w*h)")
    print("  aspect     : w/h")
    print("  center     : 中心点归一化坐标 0~1")


def print_filter_analysis(components):

    print()
    print("========== ESP32 FILTER ANALYSIS ==========")

    # 当前 ESP32 S12-2 中的大致过滤条件
    MIN_AREA = 20
    MAX_AREA_RATIO = 0.60

    MIN_W = 4
    MIN_H = 4

    MIN_FILL = 0.20
    MAX_FILL = 0.95

    MIN_ASPECT = 0.20
    MAX_ASPECT = 1.20

    if not components:
        return

    print()

    for c in components:

        reasons = []

        if c["area"] < MIN_AREA:
            reasons.append("area<20")

        if c["area_ratio"] > MAX_AREA_RATIO:
            reasons.append("area>60%")

        if c["w"] < MIN_W:
            reasons.append("w<4")

        if c["h"] < MIN_H:
            reasons.append("h<4")

        if c["fill"] < MIN_FILL:
            reasons.append("fill<0.20")

        if c["fill"] > MAX_FILL:
            reasons.append("fill>0.95")

        if c["aspect"] < MIN_ASPECT:
            reasons.append("aspect<0.20")

        if c["aspect"] > MAX_ASPECT:
            reasons.append("aspect>1.20")

        if reasons:
            result = "REJECT"
            reason_text = ", ".join(reasons)
        else:
            result = "PASS"
            reason_text = "-"

        print(
            f"ID {c['label']:3d}: "
            f"{result:6s} "
            f"area={c['area']:5d} "
            f"bbox=({c['x']},{c['y']},{c['w']},{c['h']}) "
            f"fill={c['fill']:.3f} "
            f"aspect={c['aspect']:.3f} "
            f"reason={reason_text}"
        )


def save_mask(mask, output):
    cv2.imwrite(output, mask)
    print(f"[OUTPUT] mask   : {output}")


def save_component_visualization(
    img,
    mask,
    components,
    output
):

    result = img.copy()

    for index, c in enumerate(components):

        x = c["x"]
        y = c["y"]
        w = c["w"]
        h = c["h"]

        # 所有连通区域都画出来
        cv2.rectangle(
            result,
            (x, y),
            (x + w - 1, y + h - 1),
            (0, 0, 255),
            1
        )

        # 编号
        text = str(index + 1)

        cv2.putText(
            result,
            text,
            (x, max(10, y - 2)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.35,
            (0, 0, 255),
            1,
            cv2.LINE_AA
        )

        # 中心点
        cx = int(c["cx"])
        cy = int(c["cy"])

        cv2.circle(
            result,
            (cx, cy),
            2,
            (255, 0, 0),
            -1
        )

    cv2.imwrite(output, result)

    print(f"[OUTPUT] result : {output}")


def print_summary(img, mask, components):

    height, width = mask.shape

    total_pixels = width * height

    skin_pixels = int(np.count_nonzero(mask))

    skin_ratio = skin_pixels / total_pixels

    print()
    print("========== IMAGE ==========")
    print(f"size       : {width} x {height}")
    print(f"pixels     : {total_pixels}")

    print()
    print("========== SKIN ==========")
    print(f"skin pixels: {skin_pixels}")
    print(f"skin ratio : {skin_ratio * 100:.1f}%")

    print()
    print("========== COMPONENT SUMMARY ==========")

    if components:

        largest = components[0]

        print(
            f"largest    : ID {largest['label']}, "
            f"area={largest['area']} "
            f"({largest['area_ratio'] * 100:.1f}%)"
        )

        print(
            f"bbox       : "
            f"x={largest['x']} "
            f"y={largest['y']} "
            f"w={largest['w']} "
            f"h={largest['h']}"
        )

        print(
            f"center     : "
            f"({largest['cx_norm']:.2f}, "
            f"{largest['cy_norm']:.2f})"
        )


def main():

    parser = argparse.ArgumentParser(
        description="Analyze ESP32-CAM skin-color connected components"
    )

    parser.add_argument(
        "--url",
        default=DEFAULT_URL,
        help="ESP32-CAM capture URL"
    )

    parser.add_argument(
        "--input",
        default=None,
        help="Use local image instead of HTTP capture"
    )

    parser.add_argument(
        "--output",
        default="/tmp/cam_analysis.jpg",
        help="Component visualization output"
    )

    parser.add_argument(
        "--mask",
        default="/tmp/cam_analysis_mask.jpg",
        help="Skin mask output"
    )

    args = parser.parse_args()

    original = "/tmp/cam_original.jpg"

    # --------------------------------------------------
    # 1. 获取图片
    # --------------------------------------------------

    if args.input:

        image_path = args.input

        print(f"[INPUT] local image: {image_path}")

    else:

        image_path = original

        if not download_image(args.url, image_path):
            return 1

    # --------------------------------------------------
    # 2. 读取图片
    # --------------------------------------------------

    img = cv2.imread(image_path)

    if img is None:

        print(
            f"[ERROR] cannot read image: {image_path}"
        )

        return 1

    # --------------------------------------------------
    # 3. Skin mask
    # --------------------------------------------------

    mask = make_skin_mask(img)

    # --------------------------------------------------
    # 4. Connected components
    # --------------------------------------------------

    components = analyze_components(mask)

    # --------------------------------------------------
    # 5. 输出总体信息
    # --------------------------------------------------

    print_summary(
        img,
        mask,
        components
    )

    # --------------------------------------------------
    # 6. 输出所有组件
    # --------------------------------------------------

    print_components(
        components,
        img.shape[1],
        img.shape[0]
    )

    # --------------------------------------------------
    # 7. 按 ESP32 当前过滤规则分析
    # --------------------------------------------------

    print_filter_analysis(
        components
    )

    # --------------------------------------------------
    # 8. 保存 mask
    # --------------------------------------------------

    save_mask(
        mask,
        args.mask
    )

    # --------------------------------------------------
    # 9. 保存可视化结果
    # --------------------------------------------------

    save_component_visualization(
        img,
        mask,
        components,
        args.output
    )

    print()
    print("========== DONE ==========")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())