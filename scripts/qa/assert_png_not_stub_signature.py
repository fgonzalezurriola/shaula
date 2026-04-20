#!/usr/bin/env python3
"""Reject known stub PNG signature used by old runtime stub path."""

from __future__ import annotations

import os
import sys


STUB_SIGNATURE = bytes(
    [
        0x89,
        0x50,
        0x4E,
        0x47,
        0x0D,
        0x0A,
        0x1A,
        0x0A,
        0x00,
        0x00,
        0x00,
        0x0D,
        0x49,
        0x48,
        0x44,
        0x52,
        0x00,
        0x00,
        0x00,
        0x01,
        0x00,
        0x00,
        0x00,
        0x01,
        0x08,
        0x06,
        0x00,
        0x00,
        0x00,
        0x1F,
        0x15,
        0xC4,
        0x89,
        0x00,
        0x00,
        0x00,
        0x0D,
        0x49,
        0x44,
        0x41,
        0x54,
        0x78,
        0x9C,
        0x63,
        0x60,
        0x00,
        0x00,
        0x00,
        0x02,
        0x00,
        0x01,
        0xE5,
        0x27,
        0xD4,
        0xA2,
        0x00,
        0x00,
        0x00,
        0x49,
        0x45,
        0x4E,
        0x44,
        0xAE,
        0x42,
        0x60,
        0x82,
    ]
)


def main() -> int:
    if len(sys.argv) != 2:
        print(
            "usage: python3 scripts/qa/assert_png_not_stub_signature.py <png-path>",
            file=sys.stderr,
        )
        return 2

    png_path = sys.argv[1]
    if not os.path.isfile(png_path):
        print(f"ERR_CAPTURE_PNG_NOT_FOUND path={png_path}", file=sys.stderr)
        return 1

    with open(png_path, "rb") as f:
        data = f.read()

    if len(data) < len(STUB_SIGNATURE):
        print(f"ok capture_png_not_stub_signature path={png_path} bytes={len(data)}")
        return 0

    if data[: len(STUB_SIGNATURE)] == STUB_SIGNATURE:
        print(
            f"ERR_CAPTURE_STUB_SIGNATURE_DETECTED path={png_path} signature=stub-v1x1",
            file=sys.stderr,
        )
        return 1

    print(f"ok capture_png_not_stub_signature path={png_path} bytes={len(data)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
