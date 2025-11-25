#!/usr/bin/env python3
"""
Generate multi-size PNG icons for the web UI and mobile apps.

This script reads the canonical logo stored at data/carlightsync.png
and creates resized variants for the firmware (data/icons) and the
Capacitor resources folder. Run it whenever the source logo changes.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from io import BytesIO
import base64

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - Pillow might be missing locally
    print("Pillow is required. Install it with: pip install Pillow", file=sys.stderr)
    raise SystemExit(1) from exc


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SIZES = [32, 64, 128, 192, 256, 512, 1024]
PNG_OUTPUT_TARGETS = [
    (PROJECT_ROOT / "data/icons", "carlightsync-{size}.png"),
    (PROJECT_ROOT / "mobile.app/resources/icons", "carlightsync-{size}.png"),
]
CAPACITOR_BASE_ICON = (PROJECT_ROOT / "mobile.app/resources/icon.png", 1024)
SVG_TARGETS = {
    PROJECT_ROOT
    / "mobile.app/resources/icons/icon-only.svg": {"background": None, "scale": 0.9},
    PROJECT_ROOT
    / "mobile.app/resources/icons/icon-foreground.svg": {
        "background": None,
        "scale": 0.8,
    },
    PROJECT_ROOT
    / "mobile.app/resources/icons/icon-background.svg": {
        "background": "#111827",
        "scale": 0.6,
    },
    PROJECT_ROOT
    / "mobile.app/resources/splash.svg": {"background": "#050505", "scale": 0.7},
}
SVG_SIZE = 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create resized logo assets.")
    parser.add_argument(
        "--source",
        default=str(PROJECT_ROOT / "data" / "carlightsync.png"),
        help="Source PNG logo (default: data/carlightsync.png relative to project root)",
    )
    parser.add_argument(
        "--sizes",
        nargs="*",
        type=int,
        default=None,
        help="Optional custom sizes (comma separated). Defaults to %s."
        % DEFAULT_SIZES,
    )
    return parser.parse_args()


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def image_data_uri(image: Image.Image, size: int) -> str:
    buffer = BytesIO()
    image.resize((size, size), Image.LANCZOS).save(buffer, format="PNG")
    encoded = base64.b64encode(buffer.getvalue()).decode("ascii")
    return f"data:image/png;base64,{encoded}"


def write_svg_icon(
    dest: Path, image: Image.Image, background: str | None, scale: float
) -> None:
    ensure_parent(dest)
    icon_size = SVG_SIZE
    img_size = int(icon_size * scale)
    offset = (icon_size - img_size) // 2
    data_uri = image_data_uri(image, img_size)
    rect = (
        f'  <rect width="{icon_size}" height="{icon_size}" fill="{background}"/>\n'
        if background
        else ""
    )
    image_tag = (
        f'  <image href="{data_uri}" x="{offset}" y="{offset}" '
        f'width="{img_size}" height="{img_size}" preserveAspectRatio="xMidYMid meet"/>\n'
    )
    svg = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{icon_size}" '
        f'height="{icon_size}" viewBox="0 0 {icon_size} {icon_size}">\n'
        f"{rect}{image_tag}</svg>\n"
    )
    dest.write_text(svg, encoding="utf-8")
    print(f"  -> wrote {dest} ({'SVG'})")


def main() -> int:
    args = parse_args()
    source_path = Path(args.source)
    if not source_path.exists():
        print(f"Source logo not found: {source_path}", file=sys.stderr)
        return 1

    sizes = args.sizes or DEFAULT_SIZES
    sizes = sorted(set(sizes))

    base_image = Image.open(source_path).convert("RGBA")
    print(f"Loaded logo {source_path} ({base_image.width}x{base_image.height})")

    for size in sizes:
        resized = base_image.resize((size, size), Image.LANCZOS)
        for dest_dir, name_pattern in PNG_OUTPUT_TARGETS:
            dest = dest_dir / name_pattern.format(size=size)
            ensure_parent(dest)
            resized.save(dest, format="PNG")
            print(f"  -> wrote {dest} ({size}x{size})")

    # Generate Capacitor base icon (1024x1024) used by capacitor-assets
    dest_icon_path = CAPACITOR_BASE_ICON[0]
    ensure_parent(dest_icon_path)
    cap_size = CAPACITOR_BASE_ICON[1]
    capacitor_icon = base_image.resize((cap_size, cap_size), Image.LANCZOS)
    capacitor_icon.save(dest_icon_path, format="PNG")
    print(f"  -> wrote {dest_icon_path} ({cap_size}x{cap_size})")

    # Generate SVG wrappers for Capacitor Android adaptive icons and splash
    for dest, params in SVG_TARGETS.items():
        write_svg_icon(dest, base_image, params["background"], params["scale"])

    print("Icon generation complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
