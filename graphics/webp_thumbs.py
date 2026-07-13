#!/usr/bin/env python3
"""Convert the exported module browser thumbnails from PNG to WebP.

rack_ui_smoke --export-thumbnails writes PNG (stb_image_write). PNGs of the
panel renders are ~73 MB total; lossy WebP at q80 cuts that ~60-70% with no
visible difference at tile size. Run after every thumbnail export, before
committing graphics/browser-thumbs. Android's BitmapFactory decodes WebP
natively (the loader in ModuleBrowserSheet.kt looks for .webp).
"""
import os
import sys
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "browser-thumbs")
QUALITY = 80


def main():
    before = after = 0
    n = 0
    for cur, _dirs, files in os.walk(ROOT):
        for fn in files:
            if not fn.endswith(".png"):
                continue
            src = os.path.join(cur, fn)
            dst = src[:-4] + ".webp"
            before += os.path.getsize(src)
            Image.open(src).save(dst, "WEBP", quality=QUALITY, method=6)
            os.remove(src)
            after += os.path.getsize(dst)
            n += 1
    print(f"{n} thumbnails: {before/1048576:.1f} MB PNG -> {after/1048576:.1f} MB WebP")


if __name__ == "__main__":
    sys.exit(main())
