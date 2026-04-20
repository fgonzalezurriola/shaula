#!/usr/bin/env python3
"""Deterministic runtime capture helper used by QA only.

This is intentionally external to the backend to validate that production
capture path crosses a runtime command boundary instead of writing hardcoded
PNG bytes directly in Zig.
"""

from __future__ import annotations

import argparse
import binascii
import os
import struct
import time
import zlib


def _chunk(tag: bytes, payload: bytes) -> bytes:
    crc = binascii.crc32(tag + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)


def _parse_geometry(geometry: str) -> tuple[int, int] | None:
    if not geometry:
        return None

    try:
        _origin, size = geometry.split(" ", 1)
        width_s, height_s = size.split("x", 1)
        width = int(width_s)
        height = int(height_s)
    except ValueError:
        return None

    if width <= 0 or height <= 0:
        return None

    return width, height


def _resolve_dimensions(mode: str, geometry: str | None = None) -> tuple[int, int]:
    parsed_geometry = _parse_geometry(geometry or "")
    if mode == "area" and parsed_geometry is not None:
        return parsed_geometry

    if mode == "area":
        return 640, 360
    if mode == "window":
        return 1280, 720
    return 1920, 1080


def _color_for_pixel(x: int, y: int, fixture: str) -> tuple[int, int, int, int]:
    if fixture == "colorful-grid":
        palette = (
            (255, 0, 0, 255),
            (0, 255, 0, 255),
            (0, 128, 255, 255),
            (255, 255, 0, 255),
            (255, 0, 255, 255),
            (0, 255, 255, 255),
        )
        block = 32
        idx = ((x // block) + (y // block)) % len(palette)
        return palette[idx]

    return 255, 0, 0, 255


def _is_panel_hidden() -> bool:
    panel_hidden = os.environ.get("SHAULA_PANEL_HIDDEN", "")
    panel_state = os.environ.get("SHAULA_PANEL_STATE", "")
    token_file = os.environ.get("SHAULA_PANEL_HIDDEN_TOKEN_FILE", "")

    if panel_hidden.strip().lower() in {"1", "true", "yes", "on"}:
        return True
    if panel_state.strip().lower() == "hidden":
        return True
    if token_file and os.path.exists(token_file):
        return True
    return False


def _build_png(mode: str, fixture: str, geometry: str | None = None) -> bytes:
    width, height = _resolve_dimensions(mode, geometry)
    inject_panel_marker = os.environ.get("SHAULA_CAPTURE_INJECT_PANEL_MARKER", "").strip().lower() in {"1", "true", "yes", "on"}
    marker_visible_until_ms = os.environ.get("SHAULA_PANEL_MARKER_VISIBLE_UNTIL_MS", "").strip()
    marker_time_gate_open = True
    if marker_visible_until_ms:
        try:
            marker_time_gate_open = int(time.time() * 1000) < int(marker_visible_until_ms)
        except ValueError:
            marker_time_gate_open = True

    panel_marker_visible = inject_panel_marker and marker_time_gate_open and not _is_panel_hidden()

    compressor = zlib.compressobj(9)
    encoded_parts: list[bytes] = []

    for y in range(height):
        row = bytearray()
        row.append(0)
        for x in range(width):
            if panel_marker_visible and x < 16 and y < 16:
                row.extend((255, 0, 255, 255))
                continue
            row.extend(_color_for_pixel(x, y, fixture))
        encoded_parts.append(compressor.compress(bytes(row)))

    encoded_parts.append(compressor.flush())
    compressed = b"".join(encoded_parts)
    ihdr = _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    idat = _chunk(b"IDAT", compressed)
    iend = _chunk(b"IEND", b"")
    return b"\x89PNG\r\n\x1a\n" + ihdr + idat + iend


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", required=True)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--geometry", default=None)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    fixture = os.environ.get("SHAULA_CAPTURE_FIXTURE", "colorful-grid")

    parent = os.path.dirname(args.output)
    if parent:
        os.makedirs(parent, exist_ok=True)

    with open(args.output, "wb") as f:
        f.write(_build_png(args.mode, fixture, args.geometry))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
