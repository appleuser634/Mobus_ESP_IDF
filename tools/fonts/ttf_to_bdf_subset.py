#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

import freetype


@dataclass(frozen=True)
class Glyph:
    codepoint: int
    dwidth_x: int
    bbx_w: int
    bbx_h: int
    bbx_xoff: int
    bbx_yoff: int
    bitmap_rows: list[bytes]  # packed MSB-first


def parse_map_expr(expr: str) -> list[tuple[int, int]]:
    expr = expr.strip()
    if not expr:
        raise ValueError("empty map expression")
    ranges: list[tuple[int, int]] = []
    for part in expr.split(","):
        part = part.strip()
        if not part:
            continue
        if part == "*":
            ranges.append((32, 255))
            continue
        if ">" in part or "~" in part or "x" in part:
            raise ValueError(
                f"unsupported map operator in {part!r}; use simple ranges only"
            )
        if "-" in part:
            a, b = [s.strip() for s in part.split("-", 1)]
        else:
            a, b = part, part
        start = int(a[1:], 16) if a.startswith("$") else int(a, 10)
        end = int(b[1:], 16) if b.startswith("$") else int(b, 10)
        if end < start:
            start, end = end, start
        ranges.append((start, end))
    return ranges


def codepoints_from_ranges(ranges: list[tuple[int, int]]) -> list[int]:
    cps: list[int] = []
    for a, b in ranges:
        cps.extend(range(a, b + 1))
    # unique + stable
    return sorted(set(cps))


def render_glyph(face: freetype.Face, codepoint: int) -> Glyph:
    ch = chr(codepoint)
    face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    g = face.glyph
    bm = g.bitmap
    pitch = bm.pitch

    # metrics
    dwidth_x = int(g.advance.x // 64) or int(face.size.max_advance // 64) or 1

    # For BDF, BBX offsets are the position of the bitmap's lower-left corner
    # relative to the glyph origin (baseline).
    bbx_w = int(bm.width)
    bbx_h = int(bm.rows)
    bbx_xoff = int(g.bitmap_left)
    bbx_yoff = int(g.bitmap_top) - bbx_h

    # bitmap data (packed rows, MSB-first)
    row_bytes = int(math.ceil(bbx_w / 8.0)) if bbx_w > 0 else 0
    rows: list[bytes] = []
    if bbx_w > 0 and bbx_h > 0 and row_bytes > 0:
        buf = bytes(bm.buffer)
        for y in range(bbx_h):
            start = y * pitch
            rows.append(buf[start : start + row_bytes])
    else:
        rows = [b"" for _ in range(bbx_h)]

    return Glyph(
        codepoint=codepoint,
        dwidth_x=dwidth_x,
        bbx_w=bbx_w,
        bbx_h=bbx_h,
        bbx_xoff=bbx_xoff,
        bbx_yoff=bbx_yoff,
        bitmap_rows=rows,
    )


def bdf_hex_line(row: bytes) -> str:
    return "".join(f"{b:02X}" for b in row) if row else "00"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", required=True, type=Path)
    ap.add_argument("--map", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--px", default=24, type=int)
    ap.add_argument("--dpi", default=72, type=int)
    ap.add_argument("--fontname", default="x14y24pxHeadUpDaisy")
    args = ap.parse_args()

    map_expr = args.map.read_text(encoding="utf-8").strip()
    ranges = parse_map_expr(map_expr)
    cps = codepoints_from_ranges(ranges)

    face = freetype.Face(str(args.ttf))
    face.set_char_size(0, args.px * 64, args.dpi, args.dpi)

    # ascent/descent (BDF wants positive values)
    asc = int(face.size.ascender // 64)
    desc = int(-face.size.descender // 64)

    glyphs: list[Glyph] = []
    max_w = 0
    max_h = 0
    for cp in cps:
        try:
            glyph = render_glyph(face, cp)
        except Exception:
            # Skip glyphs that the font truly can't render.
            continue
        glyphs.append(glyph)
        max_w = max(max_w, glyph.bbx_w)
        max_h = max(max_h, glyph.bbx_h)

    # BDF header
    lines: list[str] = []
    lines.append("STARTFONT 2.1")
    lines.append(f"FONT -FreeType-{args.fontname}-Regular-R-Normal--{args.px}-{args.px*10}-{args.dpi}-{args.dpi}-C-0-ISO10646-1")
    lines.append(f"SIZE {args.px} {args.dpi} {args.dpi}")
    lines.append(f"FONTBOUNDINGBOX {max_w} {max_h} 0 {-desc}")
    lines.append("STARTPROPERTIES 2")
    lines.append(f"FONT_ASCENT {asc}")
    lines.append(f"FONT_DESCENT {desc}")
    lines.append("ENDPROPERTIES")
    lines.append(f"CHARS {len(glyphs)}")

    for g in glyphs:
        lines.append(f"STARTCHAR uni{g.codepoint:04X}")
        lines.append(f"ENCODING {g.codepoint}")
        lines.append("SWIDTH 0 0")
        lines.append(f"DWIDTH {g.dwidth_x} 0")
        lines.append(f"BBX {g.bbx_w} {g.bbx_h} {g.bbx_xoff} {g.bbx_yoff}")
        lines.append("BITMAP")
        if g.bbx_h == 0:
            lines.append("00")
        else:
            for row in g.bitmap_rows:
                lines.append(bdf_hex_line(row))
        lines.append("ENDCHAR")

    lines.append("ENDFONT")
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
