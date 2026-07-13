#!/usr/bin/env python3
"""Generate panel SVGs for the first-party RackDroid Drums pack.

Unlike regen_graphics.py's jobs (which replace non-commercial upstream art),
these modules have no upstream source: DSP and panels are both original to
RackDroid. Reuses gen_panel() so the panels match the house style of every
other regenerated pack. Knobs/ports/screws are Rack's stock component
classes, drawn from the already-original system ComponentLibrary.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from regen_graphics import gen_panel

W, H = 90.0, 380.0
OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "drums", "res"))

KNOB_X, CV_X = W * 0.35, W * 0.75
TRIG_X, TRIG2_X, OUT_X = W * 0.25, W * 0.5, W * 0.75
BOTTOM_Y = 330.0

def knob_label(y, text):
    return (KNOB_X, y - 14, 4.5, text)

def cv_label(y):
    return (CV_X, y - 14, 3.4, "CV")

def bottom_label(x, text):
    return (x, BOTTOM_Y - 14, 3.6, text)

PANELS = {
    "BD-808": [
        knob_label(90, "TUNE"), cv_label(90),
        knob_label(165, "DECAY"), cv_label(165),
        knob_label(240, "CLICK"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "SD-808": [
        knob_label(90, "TUNE"), cv_label(90),
        knob_label(165, "SNAPPY"), cv_label(165),
        knob_label(240, "DECAY"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "HH-808": [
        knob_label(90, "TONE"), cv_label(90),
        knob_label(165, "O.DECAY"),
        bottom_label(TRIG_X, "CH"), bottom_label(TRIG2_X, "OH"), bottom_label(OUT_X, "OUT"),
    ],
    "CP-808": [
        knob_label(90, "TONE"),
        knob_label(165, "TAIL"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "CB-808": [
        knob_label(90, "TUNE"),
        knob_label(165, "DECAY"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "TM-808": [
        knob_label(90, "TUNE"), cv_label(90),
        knob_label(165, "DECAY"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "BD-909": [
        knob_label(90, "TUNE"), cv_label(90),
        knob_label(165, "DECAY"), cv_label(165),
        knob_label(240, "PUNCH"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "SD-909": [
        knob_label(90, "TUNE"), cv_label(90),
        knob_label(165, "TONE"),
        knob_label(240, "SNAPPY"), cv_label(240),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "HH-909": [
        knob_label(90, "TONE"), cv_label(90),
        knob_label(165, "O.DECAY"),
        bottom_label(TRIG_X, "CH"), bottom_label(TRIG2_X, "OH"), bottom_label(OUT_X, "OUT"),
    ],
    "BD-606": [
        knob_label(90, "TUNE"), cv_label(90),
        knob_label(165, "DECAY"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "HH-606": [
        knob_label(90, "TONE"),
        knob_label(165, "O.DECAY"),
        bottom_label(TRIG_X, "CH"), bottom_label(TRIG2_X, "OH"), bottom_label(OUT_X, "OUT"),
    ],
    "SD-707": [
        knob_label(90, "TUNE"), cv_label(90),
        knob_label(165, "SNAPPY"),
        knob_label(240, "DECAY"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "CB-707": [
        knob_label(90, "TUNE"),
        knob_label(165, "DECAY"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
    "TM-505": [
        knob_label(90, "TUNE"), cv_label(90),
        knob_label(165, "DECAY"),
        bottom_label(TRIG_X, "TRIG"), bottom_label(OUT_X, "OUT"),
    ],
}

if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, texts in PANELS.items():
        svg = gen_panel(name, W, H, texts)
        fn = name.replace("-", "") + ".svg"
        with open(os.path.join(OUT_DIR, fn), "w", encoding="utf-8") as f:
            f.write(svg)
        print(f"{fn} <- {name}")
