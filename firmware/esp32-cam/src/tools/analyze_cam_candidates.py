#!/usr/bin/env python3

import urllib.request
import time
import math
from collections import defaultdict

# ============================================================
# ESP32-CAM Candidate Region Temporal Analysis
#
# Purpose:
#   Analyze ALL connected components over multiple frames.
#   Do NOT modify ESP32 firmware.
#
# Camera:
#   http://192.168.0.6/capture
#
# Image:
#   QQVGA 160x120 RGB565
#
# Thresholds:
#   A: Cb 85-130, Cr 135-175
#   B: Cb 90-125, Cr 140-170
#   C: Cb 82-128, Cr 138-176
#   D: Cb 88-132, Cr 132-172
# ============================================================


URL = "http://192.168.0.6/capture"

WIDTH = 160
HEIGHT = 120

FRAME_COUNT = 20
FRAME_INTERVAL = 0.30
TIMEOUT = 3.0

# ------------------------------------------------------------
# Thresholds
# ------------------------------------------------------------

THRESHOLDS = {
    "A": (85, 130, 135, 175),
    "B": (90, 125, 140, 170),
    "C": (82, 128, 138, 176),
    "D": (88, 132, 132, 172),
}

# ------------------------------------------------------------
# Candidate filters
#
# These are deliberately relatively permissive.
# We want to OBSERVE candidates rather than prematurely reject them.
# ------------------------------------------------------------

MIN_AREA = 20
MAX_AREA_RATIO = 0.60

MIN_W = 4
MIN_H = 4

MIN_FILL = 0.15
MAX_FILL = 0.98

MIN_ASPECT = 0.10
MAX_ASPECT = 4.00

# ------------------------------------------------------------
# Temporal matching
# ------------------------------------------------------------

# Maximum center distance for two candidates to be considered
# the same temporal track.
MAX_CENTER_DISTANCE = 25.0

# Maximum difference in size ratio.
# Used as a soft condition.
MAX_SIZE_RATIO = 2.5

# A track must appear at least this many frames to be reported
MIN_TRACK_FRAMES = 3


# ============================================================
# Utility
# ============================================================

def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def rgb565_to_rgb(pixel):
    r = ((pixel >> 11) & 0x1F) * 255 // 31
    g = ((pixel >> 5) & 0x3F) * 255 // 63
    b = (pixel & 0x1F) * 255 // 31
    return r, g, b


def rgb_to_ycbcr(r, g, b):
    # BT.601 approximate full-range conversion
    y = 0.299 * r + 0.587 * g + 0.114 * b

    cb = 128.0 - 0.168736 * r - 0.331264 * g + 0.5 * b
    cr = 128.0 + 0.5 * r - 0.418688 * g - 0.081312 * b

    return y, cb, cr


def decode_rgb565_jpeg(data):
    """
    We intentionally avoid external image libraries here.

    The ESP32-CAM /capture endpoint normally returns JPEG.
    Pillow is required to decode JPEG.

    If Pillow is unavailable, print a clear error.
    """

    try:
        from PIL import Image
        import io
    except ImportError:
        print()
        print("ERROR: Pillow is not installed.")
        print()
        print("Install it with:")
        print("  pip install pillow")
        print()
        raise SystemExit(1)

    img = Image.open(io.BytesIO(data))
    img = img.convert("RGB")

    if img.size != (WIDTH, HEIGHT):
        img = img.resize((WIDTH, HEIGHT))

    return img


def rgb_image_to_ycbcr(img):
    """
    Convert PIL RGB image to three flat arrays:
      Y, Cb, Cr
    """

    pixels = list(img.getdata())

    y_arr = [0.0] * len(pixels)
    cb_arr = [0.0] * len(pixels)
    cr_arr = [0.0] * len(pixels)

    for i, (r, g, b) in enumerate(pixels):
        y, cb, cr = rgb_to_ycbcr(r, g, b)

        y_arr[i] = y
        cb_arr[i] = cb
        cr_arr[i] = cr

    return y_arr, cb_arr, cr_arr


# ============================================================
# Skin mask
# ============================================================

def make_mask(cb, cr, threshold):
    cb_min, cb_max, cr_min, cr_max = threshold

    mask = [False] * (WIDTH * HEIGHT)

    for i in range(WIDTH * HEIGHT):
        if (
            cb_min <= cb[i] <= cb_max
            and
            cr_min <= cr[i] <= cr_max
        ):
            mask[i] = True

    return mask


# ============================================================
# 3x3 morphological opening
#
# erosion followed by dilation
# ============================================================

def erode(mask):
    out = [False] * (WIDTH * HEIGHT)

    for y in range(1, HEIGHT - 1):
        base = y * WIDTH

        for x in range(1, WIDTH - 1):

            ok = True

            for dy in (-1, 0, 1):
                row = (y + dy) * WIDTH

                for dx in (-1, 0, 1):
                    if not mask[row + x + dx]:
                        ok = False
                        break

                if not ok:
                    break

            out[base + x] = ok

    return out


def dilate(mask):
    out = [False] * (WIDTH * HEIGHT)

    for y in range(1, HEIGHT - 1):
        base = y * WIDTH

        for x in range(1, WIDTH - 1):

            found = False

            for dy in (-1, 0, 1):
                row = (y + dy) * WIDTH

                for dx in (-1, 0, 1):
                    if mask[row + x + dx]:
                        found = True
                        break

                if found:
                    break

            out[base + x] = found

    return out


def morphological_opening(mask):
    return dilate(erode(mask))


# ============================================================
# Connected components
# ============================================================

def connected_components(mask):
    """
    8-neighbor BFS.

    Returns:
        list of dict:
          x, y, w, h, area
          pixels = list of pixel indexes
    """

    visited = [False] * (WIDTH * HEIGHT)

    components = []

    neighbors = (
        (-1, -1), (0, -1), (1, -1),
        (-1,  0),          (1,  0),
        (-1,  1), (0,  1), (1,  1),
    )

    for y0 in range(HEIGHT):
        for x0 in range(WIDTH):

            start = y0 * WIDTH + x0

            if not mask[start] or visited[start]:
                continue

            queue = [start]
            visited[start] = True

            pixels = []

            min_x = x0
            max_x = x0
            min_y = y0
            max_y = y0

            head = 0

            while head < len(queue):

                idx = queue[head]
                head += 1

                pixels.append(idx)

                y = idx // WIDTH
                x = idx - y * WIDTH

                if x < min_x:
                    min_x = x
                if x > max_x:
                    max_x = x
                if y < min_y:
                    min_y = y
                if y > max_y:
                    max_y = y

                for dx, dy in neighbors:

                    nx = x + dx
                    ny = y + dy

                    if (
                        nx < 0
                        or nx >= WIDTH
                        or ny < 0
                        or ny >= HEIGHT
                    ):
                        continue

                    ni = ny * WIDTH + nx

                    if mask[ni] and not visited[ni]:
                        visited[ni] = True
                        queue.append(ni)

            w = max_x - min_x + 1
            h = max_y - min_y + 1
            area = len(pixels)

            components.append({
                "x": min_x,
                "y": min_y,
                "w": w,
                "h": h,
                "area": area,
                "pixels": pixels,
            })

    return components


# ============================================================
# Candidate feature calculation
# ============================================================

def calculate_features(comp, y_arr, cb_arr, cr_arr):

    x = comp["x"]
    y = comp["y"]
    w = comp["w"]
    h = comp["h"]
    area = comp["area"]

    bbox_area = w * h

    fill = area / bbox_area if bbox_area > 0 else 0.0
    aspect = w / h if h > 0 else 999.0

    cx = x + w / 2.0
    cy = y + h / 2.0

    cx_norm = cx / WIDTH
    cy_norm = cy / HEIGHT

    # --------------------------------------------------------
    # Border ratio
    # --------------------------------------------------------

    border_count = 0

    for idx in comp["pixels"]:
        py = idx // WIDTH
        px = idx - py * WIDTH

        if (
            px == 0
            or py == 0
            or px == WIDTH - 1
            or py == HEIGHT - 1
        ):
            border_count += 1

    border_ratio = border_count / area if area > 0 else 1.0

    # --------------------------------------------------------
    # Y statistics
    # --------------------------------------------------------

    ys = [y_arr[i] for i in comp["pixels"]]

    y_mean = sum(ys) / len(ys)

    y_var = sum(
        (v - y_mean) ** 2
        for v in ys
    ) / len(ys)

    y_std = math.sqrt(y_var)

    y_min = min(ys)
    y_max = max(ys)

    # --------------------------------------------------------
    # Cb / Cr statistics
    # --------------------------------------------------------

    cbs = [cb_arr[i] for i in comp["pixels"]]
    crs = [cr_arr[i] for i in comp["pixels"]]

    cb_mean = sum(cbs) / len(cbs)
    cr_mean = sum(crs) / len(crs)

    cb_var = sum(
        (v - cb_mean) ** 2
        for v in cbs
    ) / len(cbs)

    cr_var = sum(
        (v - cr_mean) ** 2
        for v in crs
    ) / len(crs)

    cb_std = math.sqrt(cb_var)
    cr_std = math.sqrt(cr_var)

    # --------------------------------------------------------
    # Edge ratio
    #
    # Count transitions between candidate and non-candidate
    # around each component pixel.
    # --------------------------------------------------------

    edge_count = 0

    comp_set = set(comp["pixels"])

    for idx in comp["pixels"]:

        py = idx // WIDTH
        px = idx - py * WIDTH

        different = False

        for dx, dy in (
            (-1, 0),
            (1, 0),
            (0, -1),
            (0, 1),
        ):
            nx = px + dx
            ny = py + dy

            if (
                nx < 0
                or nx >= WIDTH
                or ny < 0
                or ny >= HEIGHT
            ):
                different = True
                break

            ni = ny * WIDTH + nx

            if ni not in comp_set:
                different = True
                break

        if different:
            edge_count += 1

    edge_ratio = edge_count / area if area > 0 else 1.0

    # --------------------------------------------------------
    # Vertical / horizontal Y contrast
    # --------------------------------------------------------

    top_values = []
    bottom_values = []

    left_values = []
    right_values = []

    for idx in comp["pixels"]:

        py = idx // WIDTH
        px = idx - py * WIDTH

        rel_x = (px - x) / max(1, w - 1)
        rel_y = (py - y) / max(1, h - 1)

        value = y_arr[idx]

        if rel_y < 0.5:
            top_values.append(value)
        else:
            bottom_values.append(value)

        if rel_x < 0.5:
            left_values.append(value)
        else:
            right_values.append(value)

    def mean_or_zero(values):
        return sum(values) / len(values) if values else 0.0

    y_top = mean_or_zero(top_values)
    y_bottom = mean_or_zero(bottom_values)

    y_left = mean_or_zero(left_values)
    y_right = mean_or_zero(right_values)

    contrast_tb = abs(y_top - y_bottom)
    contrast_lr = abs(y_left - y_right)

    # --------------------------------------------------------
    # Area ratio
    # --------------------------------------------------------

    area_ratio = area / (WIDTH * HEIGHT)

    # --------------------------------------------------------
    # Feature dictionary
    # --------------------------------------------------------

    result = dict(comp)

    result.update({
        "cx": cx,
        "cy": cy,

        "cx_norm": cx_norm,
        "cy_norm": cy_norm,

        "fill": fill,
        "aspect": aspect,
        "area_ratio": area_ratio,

        "border_ratio": border_ratio,

        "y_mean": y_mean,
        "y_std": y_std,
        "y_min": y_min,
        "y_max": y_max,

        "cb_mean": cb_mean,
        "cb_std": cb_std,

        "cr_mean": cr_mean,
        "cr_std": cr_std,

        "edge": edge_ratio,

        "contrast_tb": contrast_tb,
        "contrast_lr": contrast_lr,
    })

    return result


# ============================================================
# Candidate filter
# ============================================================

def passes_basic_filter(c):

    if c["area"] < MIN_AREA:
        return False

    if c["area_ratio"] > MAX_AREA_RATIO:
        return False

    if c["w"] < MIN_W or c["h"] < MIN_H:
        return False

    if c["fill"] < MIN_FILL or c["fill"] > MAX_FILL:
        return False

    if c["aspect"] < MIN_ASPECT or c["aspect"] > MAX_ASPECT:
        return False

    return True


# ============================================================
# Capture frame
# ============================================================

def capture_frame(frame_no):

    print(f"  Capturing frame {frame_no:02d}...", end="", flush=True)

    try:
        with urllib.request.urlopen(
            URL,
            timeout=TIMEOUT
        ) as response:

            data = response.read()

    except Exception as e:

        print(f" ERROR: {e}")

        return None

    try:
        img = decode_rgb565_jpeg(data)

    except Exception as e:

        print(f" ERROR decoding JPEG: {e}")

        return None

    print(f" OK ({len(data)} bytes)")

    return img


# ============================================================
# Temporal track
# ============================================================

class Track:

    def __init__(self, track_id, candidate, frame_no):

        self.id = track_id

        self.first_frame = frame_no
        self.last_frame = frame_no

        self.frames = [frame_no]

        self.candidates = [candidate]

    def add(self, candidate, frame_no):

        self.last_frame = frame_no

        self.frames.append(frame_no)

        self.candidates.append(candidate)

    @property
    def count(self):
        return len(self.frames)

    @property
    def first(self):
        return self.candidates[0]

    @property
    def last(self):
        return self.candidates[-1]

    def statistics(self):

        cs = self.candidates

        def avg(key):
            return sum(c[key] for c in cs) / len(cs)

        def std(key):

            m = avg(key)

            return math.sqrt(
                sum(
                    (c[key] - m) ** 2
                    for c in cs
                ) / len(cs)
            )

        return {
            "frames": self.count,

            "cx_mean": avg("cx"),
            "cx_std": std("cx"),

            "cy_mean": avg("cy"),
            "cy_std": std("cy"),

            "w_mean": avg("w"),
            "w_std": std("w"),

            "h_mean": avg("h"),
            "h_std": std("h"),

            "area_mean": avg("area"),
            "area_std": std("area"),

            "fill_mean": avg("fill"),
            "fill_std": std("fill"),

            "aspect_mean": avg("aspect"),
            "aspect_std": std("aspect"),

            "y_std_mean": avg("y_std"),
            "edge_mean": avg("edge"),

            "border_mean": avg("border_ratio"),

            "contrast_tb_mean": avg("contrast_tb"),
            "contrast_lr_mean": avg("contrast_lr"),

            "cb_std_mean": avg("cb_std"),
            "cr_std_mean": avg("cr_std"),
        }


def candidate_distance(a, b):

    dx = a["cx"] - b["cx"]
    dy = a["cy"] - b["cy"]

    return math.sqrt(dx * dx + dy * dy)


def size_ratio(a, b):

    area_a = max(1.0, float(a["area"]))
    area_b = max(1.0, float(b["area"]))

    r = max(area_a, area_b) / min(area_a, area_b)

    return r


def match_candidate_to_track(candidate, tracks):

    best_track = None
    best_distance = None

    for track in tracks:

        last = track.last

        distance = candidate_distance(
            candidate,
            last
        )

        ratio = size_ratio(
            candidate,
            last
        )

        if distance > MAX_CENTER_DISTANCE:
            continue

        if ratio > MAX_SIZE_RATIO:
            continue

        if (
            best_distance is None
            or distance < best_distance
        ):
            best_distance = distance
            best_track = track

    return best_track


# ============================================================
# Analyze one threshold
# ============================================================

def analyze_threshold(
    threshold_name,
    threshold,
    frames
):

    print()
    print("=" * 90)
    print(f"THRESHOLD {threshold_name}")
    print("=" * 90)

    all_frame_candidates = []

    for frame_no, img in enumerate(frames, start=1):

        if img is None:
            all_frame_candidates.append([])
            continue

        y_arr, cb_arr, cr_arr = rgb_image_to_ycbcr(img)

        mask = make_mask(
            cb_arr,
            cr_arr,
            threshold
        )

        opened = morphological_opening(mask)

        components = connected_components(opened)

        candidates = []

        for comp in components:

            c = calculate_features(
                comp,
                y_arr,
                cb_arr,
                cr_arr
            )

            if passes_basic_filter(c):
                candidates.append(c)

        candidates.sort(
            key=lambda c: c["area"],
            reverse=True
        )

        all_frame_candidates.append(candidates)

        print()
        print(
            f"FRAME {frame_no:02d}: "
            f"{len(components)} components, "
            f"{len(candidates)} candidates"
        )

        for idx, c in enumerate(candidates, start=1):

            print(
                f"  #{idx:02d} "
                f"x={c['x']:3d} "
                f"y={c['y']:3d} "
                f"w={c['w']:3d} "
                f"h={c['h']:3d} "
                f"area={c['area']:4d} "
                f"fill={c['fill']:.2f} "
                f"asp={c['aspect']:.2f} "
                f"cx={c['cx']:.1f} "
                f"cy={c['cy']:.1f} "
                f"Ystd={c['y_std']:.1f} "
                f"edge={c['edge']:.2f} "
                f"border={c['border_ratio']:.2f}"
            )

    # --------------------------------------------------------
    # Temporal matching
    # --------------------------------------------------------

    tracks = []
    next_track_id = 1

    for frame_no, candidates in enumerate(
        all_frame_candidates,
        start=1
    ):

        # Important:
        # A track can only receive one candidate from a frame.
        # Track assignment is greedy based on closest center.

        used_tracks = set()

        candidates_sorted = sorted(
            candidates,
            key=lambda c: c["area"],
            reverse=True
        )

        for candidate in candidates_sorted:

            available_tracks = [
                t for t in tracks
                if t.id not in used_tracks
                and t.last_frame == frame_no - 1
            ]

            track = match_candidate_to_track(
                candidate,
                available_tracks
            )

            if track is not None:

                track.add(
                    candidate,
                    frame_no
                )

                used_tracks.add(track.id)

            else:

                track = Track(
                    next_track_id,
                    candidate,
                    frame_no
                )

                tracks.append(track)

                used_tracks.add(track.id)

                next_track_id += 1

    # --------------------------------------------------------
    # Track summary
    # --------------------------------------------------------

    valid_tracks = [
        t for t in tracks
        if t.count >= MIN_TRACK_FRAMES
    ]

    valid_tracks.sort(
        key=lambda t: t.count,
        reverse=True
    )

    print()
    print("-" * 90)
    print(
        f"TEMPORAL TRACKS "
        f"(minimum {MIN_TRACK_FRAMES} frames)"
    )
    print("-" * 90)

    if not valid_tracks:

        print("No temporal tracks found.")

        return

    for rank, track in enumerate(
        valid_tracks,
        start=1
    ):

        s = track.statistics()

        print()
        print(
            f"TRACK #{track.id:02d} "
            f"[rank {rank}]"
        )

        print(
            f"  frames      : "
            f"{s['frames']} "
            f"({track.first_frame:02d}"
            f"-"
            f"{track.last_frame:02d})"
        )

        print(
            f"  center X    : "
            f"mean={s['cx_mean']:.1f} "
            f"std={s['cx_std']:.1f}"
        )

        print(
            f"  center Y    : "
            f"mean={s['cy_mean']:.1f} "
            f"std={s['cy_std']:.1f}"
        )

        print(
            f"  width       : "
            f"mean={s['w_mean']:.1f} "
            f"std={s['w_std']:.1f}"
        )

        print(
            f"  height      : "
            f"mean={s['h_mean']:.1f} "
            f"std={s['h_std']:.1f}"
        )

        print(
            f"  area        : "
            f"mean={s['area_mean']:.1f} "
            f"std={s['area_std']:.1f}"
        )

        print(
            f"  fill        : "
            f"mean={s['fill_mean']:.2f} "
            f"std={s['fill_std']:.2f}"
        )

        print(
            f"  aspect      : "
            f"mean={s['aspect_mean']:.2f} "
            f"std={s['aspect_std']:.2f}"
        )

        print(
            f"  Y std       : "
            f"mean={s['y_std_mean']:.1f}"
        )

        print(
            f"  edge        : "
            f"mean={s['edge_mean']:.2f}"
        )

        print(
            f"  border      : "
            f"mean={s['border_mean']:.2f}"
        )

        print(
            f"  Y contrast  : "
            f"TB={s['contrast_tb_mean']:.1f} "
            f"LR={s['contrast_lr_mean']:.1f}"
        )

        print(
            f"  Cb std      : "
            f"{s['cb_std_mean']:.2f}"
        )

        print(
            f"  Cr std      : "
            f"{s['cr_std_mean']:.2f}"
        )


# ============================================================
# Main
# ============================================================

def main():

    print("=" * 90)
    print("ESP32-CAM ALL-CANDIDATE TEMPORAL ANALYSIS")
    print("=" * 90)

    print()
    print(f"Camera URL : {URL}")
    print(f"Resolution : {WIDTH}x{HEIGHT}")
    print(f"Frames     : {FRAME_COUNT}")
    print()

    print("Thresholds:")

    for name, t in THRESHOLDS.items():

        print(
            f"  {name}: "
            f"Cb {t[0]}-{t[1]}, "
            f"Cr {t[2]}-{t[3]}"
        )

    print()
    print("IMPORTANT:")
    print("  Close /stream before running this script.")
    print("  Keep the camera fixed during this test.")
    print("  Do NOT modify ESP32 firmware.")
    print()

    # --------------------------------------------------------
    # Capture all frames first
    # --------------------------------------------------------

    frames = []

    print("=" * 90)
    print("CAPTURE")
    print("=" * 90)

    for frame_no in range(1, FRAME_COUNT + 1):

        img = capture_frame(frame_no)

        frames.append(img)

        if frame_no < FRAME_COUNT:
            time.sleep(FRAME_INTERVAL)

    valid_count = sum(
        1 for f in frames
        if f is not None
    )

    print()
    print(
        f"Captured {valid_count}/{FRAME_COUNT} valid frames."
    )

    if valid_count == 0:

        print()
        print("ERROR: No valid frames captured.")
        print("Check:")
        print("  1. ESP32-CAM is powered.")
        print("  2. http://192.168.0.6/capture works.")
        print("  3. /stream is closed.")
        print()

        return

    # --------------------------------------------------------
    # Analyze every threshold
    # --------------------------------------------------------

    for name, threshold in THRESHOLDS.items():

        analyze_threshold(
            name,
            threshold,
            frames
        )

    # --------------------------------------------------------
    # Final interpretation guide
    # --------------------------------------------------------

    print()
    print("=" * 90)
    print("HOW TO READ THE RESULT")
    print("=" * 90)

    print()
    print("The important fields are:")
    print()
    print("  frames")
    print("      How many consecutive frames contain this track.")
    print()
    print("  center X/Y std")
    print("      Smaller = more spatially stable.")
    print()
    print("  width/height std")
    print("      Smaller = more geometrically stable.")
    print()
    print("  area std")
    print("      Smaller = more stable region size.")
    print()
    print("  fill")
    print("      Near 1.0 may indicate a very solid / uniform block.")
    print()
    print("  aspect")
    print("      Width / height.")
    print()
    print("  Y std")
    print("      Very small values may indicate a visually uniform area.")
    print()
    print("  edge")
    print("      Indicates boundary complexity.")
    print()
    print("  border")
    print("      High value means the candidate touches image borders.")
    print()
    print("  Y contrast")
    print("      Difference between top/bottom and left/right brightness.")
    print()
    print("IMPORTANT:")
    print("  A stable track is NOT automatically a person.")
    print("  This script is for collecting evidence.")
    print("  We will use the data to design the red-box score.")
    print()

    print("=" * 90)
    print("ANALYSIS COMPLETE")
    print("=" * 90)


if __name__ == "__main__":
    main()