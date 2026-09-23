import cv2
import urllib.request
import numpy as np
import json
import sys
import time

CAMERA_URL = "http://192.168.0.6/capture"


def fetch_image(url):
    with urllib.request.urlopen(url, timeout=5) as response:
        data = response.read()

    arr = np.frombuffer(data, dtype=np.uint8)
    img = cv2.imdecode(arr, cv2.IMREAD_COLOR)

    if img is None:
        raise RuntimeError("无法解析摄像头 JPEG")

    return img


def detect_red_box(img):
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)

    # 红色分两段
    mask1 = cv2.inRange(
        hsv,
        np.array([0, 100, 80]),
        np.array([10, 255, 255])
    )

    mask2 = cv2.inRange(
        hsv,
        np.array([170, 100, 80]),
        np.array([180, 255, 255])
    )

    mask = cv2.bitwise_or(mask1, mask2)

    contours, _ = cv2.findContours(
        mask,
        cv2.RETR_EXTERNAL,
        cv2.CHAIN_APPROX_SIMPLE
    )

    candidates = []

    for c in contours:
        x, y, w, h = cv2.boundingRect(c)

        area = w * h

        if w >= 10 and h >= 10 and area >= 100:
            candidates.append((area, x, y, w, h))

    if not candidates:
        return None

    candidates.sort(reverse=True)

    _, x, y, w, h = candidates[0]

    return {
        "x": x,
        "y": y,
        "w": w,
        "h": h,
        "cx": round(x + w / 2, 2),
        "cy": round(y + h / 2, 2),
    }


def detect_faces(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

    cascade_path = cv2.data.haarcascades + \
        "haarcascade_frontalface_default.xml"

    detector = cv2.CascadeClassifier(cascade_path)

    faces = detector.detectMultiScale(
        gray,
        scaleFactor=1.1,
        minNeighbors=4,
        minSize=(15, 15)
    )

    result = []

    for x, y, w, h in faces:
        result.append({
            "x": int(x),
            "y": int(y),
            "w": int(w),
            "h": int(h),
            "cx": round(x + w / 2, 2),
            "cy": round(y + h / 2, 2),
        })

    return result


def iou(a, b):
    if a is None or b is None:
        return 0.0

    ax1 = a["x"]
    ay1 = a["y"]
    ax2 = ax1 + a["w"]
    ay2 = ay1 + a["h"]

    bx1 = b["x"]
    by1 = b["y"]
    bx2 = bx1 + b["w"]
    by2 = by1 + b["h"]

    ix1 = max(ax1, bx1)
    iy1 = max(ay1, by1)
    ix2 = min(ax2, bx2)
    iy2 = min(ay2, by2)

    iw = max(0, ix2 - ix1)
    ih = max(0, iy2 - iy1)

    inter = iw * ih

    area_a = a["w"] * a["h"]
    area_b = b["w"] * b["h"]

    union = area_a + area_b - inter

    if union <= 0:
        return 0.0

    return inter / union


def analyze():
    print("=" * 60)
    print("ESP32-CAM 人物/人脸检测分析")
    print("=" * 60)

    print(f"[CAM] {CAMERA_URL}")

    img = fetch_image(CAMERA_URL)

    h, w = img.shape[:2]

    print(f"[IMAGE] size = {w}x{h}")

    red_box = detect_red_box(img)
    faces = detect_faces(img)

    print()
    print("[RED BOX]")

    if red_box:
        print(json.dumps(red_box, ensure_ascii=False))
    else:
        print("NOT FOUND")

    print()
    print("[OPENCV FACES]")
    print(f"count = {len(faces)}")

    for i, face in enumerate(faces):
        print(f"face[{i}] = {json.dumps(face)}")

    print()

    if red_box and faces:

        best = max(
            faces,
            key=lambda f: iou(red_box, f)
        )

        overlap = iou(red_box, best)

        print("[COMPARE]")
        print(f"best_face = {json.dumps(best)}")
        print(f"IoU = {overlap:.3f}")

        dx = red_box["cx"] - best["cx"]
        dy = red_box["cy"] - best["cy"]

        print(f"center_dx = {dx:.1f}")
        print(f"center_dy = {dy:.1f}")

    elif red_box:
        print("[COMPARE]")
        print("检测到了 ESP32 红框，但 OpenCV 没检测到人脸")

    elif faces:
        print("[COMPARE]")
        print("OpenCV 检测到了人脸，但没有检测到 ESP32 红框")

    else:
        print("[COMPARE]")
        print("两者都没有检测到")

    print()
    print("[RAW_JSON]")

    result = {
        "image": {
            "width": w,
            "height": h
        },
        "red_box": red_box,
        "faces": faces
    }

    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    try:
        analyze()
    except Exception as e:
        print(f"[ERROR] {type(e).__name__}: {e}")
        sys.exit(1)
