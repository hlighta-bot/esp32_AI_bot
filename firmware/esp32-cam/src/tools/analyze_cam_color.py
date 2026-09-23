#!/usr/bin/env python3

import argparse
import urllib.request
import cv2
import numpy as np


DEFAULT_URL = "http://192.168.0.6/capture"
DEFAULT_INPUT = "/tmp/cam_original.jpg"

# 当前 ESP32 使用的阈值
CB_MIN = 77
CB_MAX = 135
CR_MIN = 128
CR_MAX = 180


def download_image(url, output):
    print(f"[HTTP] GET {url}")

    try:
        urllib.request.urlretrieve(url, output)
    except Exception as e:
        print(f"[ERROR] download failed: {e}")
        return False

    print(f"[HTTP] saved: {output}")
    return True


def print_histogram(name, values):
    """
    每 10 个数值作为一个区间统计一次。
    """

    print()
    print(f"========== {name} DISTRIBUTION ==========")

    hist = np.bincount(
        values.astype(np.uint8),
        minlength=256
    )

    total = len(values)

    for start in range(0, 256, 10):

        end = min(start + 9, 255)

        count = int(hist[start:end + 1].sum())

        ratio = count / total * 100

        print(
            f"{start:3d}-{end:3d} : "
            f"{count:6d} "
            f"({ratio:6.2f}%)"
        )


def print_threshold_distribution(
    cb,
    cr
):

    print()
    print("========== CURRENT ESP32 THRESHOLD ==========")

    mask = (
        (cb >= CB_MIN)
        & (cb <= CB_MAX)
        & (cr >= CR_MIN)
        & (cr <= CR_MAX)
    )

    total = cb.size
    matched = int(np.count_nonzero(mask))

    print(
        f"Cb range : {CB_MIN} - {CB_MAX}"
    )

    print(
        f"Cr range : {CR_MIN} - {CR_MAX}"
    )

    print(
        f"matched  : {matched}"
    )

    print(
        f"ratio    : {matched / total * 100:.2f}%"
    )

    return mask


def print_statistics(cb, cr):

    print()
    print("========== COLOR STATISTICS ==========")

    print(
        f"Cb min={cb.min():3d} "
        f"max={cb.max():3d} "
        f"mean={cb.mean():6.2f} "
        f"median={np.median(cb):6.2f}"
    )

    print(
        f"Cr min={cr.min():3d} "
        f"max={cr.max():3d} "
        f"mean={cr.mean():6.2f} "
        f"median={np.median(cr):6.2f}"
    )


def test_threshold(
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

    total = cb.size
    count = int(np.count_nonzero(mask))

    return count, count / total * 100


def test_threshold_sets(cb, cr):

    """
    测试几组候选阈值。

    注意：
    这里只是诊断，不代表最终 ESP32 就应该使用这些值。
    """

    tests = [

        # 当前 ESP32
        (
            "CURRENT",
            77, 135,
            128, 180
        ),

        # 稍微收窄
        (
            "A",
            85, 130,
            135, 175
        ),

        # 更严格
        (
            "B",
            90, 125,
            140, 170
        ),

        # 中等
        (
            "C",
            82, 128,
            138, 176
        ),

        # 另一组
        (
            "D",
            88, 132,
            132, 172
        ),
    ]

    print()
    print("========== THRESHOLD COMPARISON ==========")

    print(
        "NAME      Cb range      Cr range      "
        "pixels       ratio"
    )

    print(
        "--------  ------------  ------------  "
        "----------   -------"
    )

    for name, cb_min, cb_max, cr_min, cr_max in tests:

        count, ratio = test_threshold(
            cb,
            cr,
            cb_min,
            cb_max,
            cr_min,
            cr_max
        )

        print(
            f"{name:8s}  "
            f"{cb_min:3d}-{cb_max:3d}      "
            f"{cr_min:3d}-{cr_max:3d}      "
            f"{count:8d}   "
            f"{ratio:6.2f}%"
        )


def save_color_visualization(
    img,
    cb,
    cr,
    output
):

    """
    生成简单的 Cb / Cr 可视化。

    左边：Cb
    右边：Cr
    """

    cb_img = cv2.normalize(
        cb,
        None,
        0,
        255,
        cv2.NORM_MINMAX
    ).astype(np.uint8)

    cr_img = cv2.normalize(
        cr,
        None,
        0,
        255,
        cv2.NORM_MINMAX
    ).astype(np.uint8)

    cb_img = cv2.applyColorMap(
        cb_img,
        cv2.COLORMAP_JET
    )

    cr_img = cv2.applyColorMap(
        cr_img,
        cv2.COLORMAP_JET
    )

    result = np.hstack(
        [cb_img, cr_img]
    )

    cv2.imwrite(
        output,
        result
    )

    print(
        f"[OUTPUT] color map : {output}"
    )


def save_mask(
    cb,
    cr,
    output
):

    mask = (
        (cb >= CB_MIN)
        & (cb <= CB_MAX)
        & (cr >= CR_MIN)
        & (cr <= CR_MAX)
    )

    mask_img = (
        mask.astype(np.uint8) * 255
    )

    cv2.imwrite(
        output,
        mask_img
    )

    print(
        f"[OUTPUT] mask      : {output}"
    )


def main():

    parser = argparse.ArgumentParser(
        description="Analyze ESP32-CAM YCrCb color distribution"
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
        "--color-output",
        default="/tmp/cam_color_map.jpg",
        help="Cb/Cr visualization"
    )

    parser.add_argument(
        "--mask-output",
        default="/tmp/cam_color_mask.jpg",
        help="Current ESP32 skin mask"
    )

    args = parser.parse_args()

    # --------------------------------------------------
    # 1. 获取图片
    # --------------------------------------------------

    if args.input:

        image_path = args.input

        print(
            f"[INPUT] local image: {image_path}"
        )

    else:

        image_path = DEFAULT_INPUT

        if not download_image(
            args.url,
            image_path
        ):
            return 1

    # --------------------------------------------------
    # 2. 读取图片
    # --------------------------------------------------

    img = cv2.imread(
        image_path
    )

    if img is None:

        print(
            f"[ERROR] cannot read: {image_path}"
        )

        return 1

    height, width = img.shape[:2]

    print()
    print("========== IMAGE ==========")
    print(
        f"size       : {width} x {height}"
    )

    print(
        f"pixels     : {width * height}"
    )

    # --------------------------------------------------
    # 3. BGR -> YCrCb
    # --------------------------------------------------

    ycrcb = cv2.cvtColor(
        img,
        cv2.COLOR_BGR2YCrCb
    )

    y = ycrcb[:, :, 0]
    cr = ycrcb[:, :, 1]
    cb = ycrcb[:, :, 2]

    # --------------------------------------------------
    # 4. 基本统计
    # --------------------------------------------------

    print_statistics(
        cb,
        cr
    )

    # --------------------------------------------------
    # 5. Cb 分布
    # --------------------------------------------------

    print_histogram(
        "Cb",
        cb.flatten()
    )

    # --------------------------------------------------
    # 6. Cr 分布
    # --------------------------------------------------

    print_histogram(
        "Cr",
        cr.flatten()
    )

    # --------------------------------------------------
    # 7. 当前 ESP32 阈值
    # --------------------------------------------------

    current_mask = print_threshold_distribution(
        cb,
        cr
    )

    # --------------------------------------------------
    # 8. 测试不同阈值
    # --------------------------------------------------

    test_threshold_sets(
        cb,
        cr
    )

    # --------------------------------------------------
    # 9. 保存当前 mask
    # --------------------------------------------------

    save_mask(
        cb,
        cr,
        args.mask_output
    )

    # --------------------------------------------------
    # 10. 保存颜色可视化
    # --------------------------------------------------

    save_color_visualization(
        img,
        cb,
        cr,
        args.color_output
    )

    # --------------------------------------------------
    # 11. 最终摘要
    # --------------------------------------------------

    print()
    print("========== SUMMARY ==========")

    print(
        f"Current threshold:"
    )

    print(
        f"  Cb = {CB_MIN} ~ {CB_MAX}"
    )

    print(
        f"  Cr = {CR_MIN} ~ {CR_MAX}"
    )

    print(
        f"Skin pixels = "
        f"{np.count_nonzero(current_mask)}"
    )

    print(
        f"Skin ratio  = "
        f"{np.count_nonzero(current_mask) / current_mask.size * 100:.2f}%"
    )

    print()
    print("Output files:")
    print(
        f"  {args.mask_output}"
    )
    print(
        f"  {args.color_output}"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())