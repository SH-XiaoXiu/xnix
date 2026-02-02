#!/usr/bin/env python3
"""

BDF 到二进制字体转换器

Usage:
  ./bdf2bin.py unifont.bdf --ascii -o ascii.c
  ./bdf2bin.py unifont.bdf --cjk -o cjk.c
  ./bdf2bin.py unifont.bdf --cjk --binary -o font_cjk.bin
"""

import argparse
import re
import struct
import sys

# Font file magic: "XFNT" in little-endian
FONT_MAGIC = 0x544E4658


def parse_bdf(bdf_path, codepoint_filter=None):
    """Parse BDF file and yield (codepoint, width, bitmap_bytes)."""
    with open(bdf_path, 'r', encoding='utf-8', errors='replace') as f:
        encoding = None
        width = 16
        in_bitmap = False
        bitmap_lines = []

        for line in f:
            line = line.strip()

            if line.startswith('STARTCHAR '):
                bitmap_lines = []
                in_bitmap = False

            elif line.startswith('ENCODING '):
                encoding = int(line[9:])

            elif line.startswith('DWIDTH '):
                parts = line.split()
                width = int(parts[1])

            elif line == 'BITMAP':
                in_bitmap = True

            elif line == 'ENDCHAR':
                if encoding is not None:
                    if codepoint_filter is None or codepoint_filter(encoding):
                        bitmap_bytes = []
                        for hex_line in bitmap_lines:
                            if width <= 8:
                                val = int(hex_line, 16) if hex_line else 0
                                bitmap_bytes.append(val & 0xFF)
                            else:
                                val = int(hex_line, 16) if hex_line else 0
                                bitmap_bytes.append((val >> 8) & 0xFF)
                                bitmap_bytes.append(val & 0xFF)

                        # Pad to 16 rows
                        if width <= 8:
                            while len(bitmap_bytes) < 16:
                                bitmap_bytes.append(0)
                            bitmap_bytes = bitmap_bytes[:16]
                        else:
                            while len(bitmap_bytes) < 32:
                                bitmap_bytes.append(0)
                            bitmap_bytes = bitmap_bytes[:32]

                        yield (encoding, width, bytes(bitmap_bytes))

                encoding = None
                width = 16
                in_bitmap = False

            elif in_bitmap and line:
                bitmap_lines.append(line)


def generate_c_array(glyphs, name, output_path):
    """Generate C source file with font data."""
    narrow = [(cp, data) for cp, w, data in glyphs if w <= 8]
    wide = [(cp, data) for cp, w, data in glyphs if w > 8]

    with open(output_path, 'w') as f:
        f.write('/* Auto-generated font data from unifont */\n')
        f.write('#include <xnix/types.h>\n\n')

        if narrow:
            f.write(f'/* {len(narrow)} narrow (8x16) glyphs */\n')
            f.write(f'const uint8_t {name}_narrow[][16] = {{\n')
            for cp, data in sorted(narrow):
                hex_data = ', '.join(f'0x{b:02X}' for b in data)
                if cp >= 0x20 and cp < 0x7F:
                    f.write(f'    [{cp}] = {{{hex_data}}},  /* \'{chr(cp)}\' */\n')
                else:
                    f.write(f'    [{cp}] = {{{hex_data}}},  /* U+{cp:04X} */\n')
            f.write('};\n')
            f.write(f'const uint32_t {name}_narrow_count = sizeof({name}_narrow) / sizeof({name}_narrow[0]);\n\n')

        if wide:
            f.write(f'/* {len(wide)} wide (16x16) glyphs */\n')
            f.write(f'const uint8_t {name}_wide[][32] = {{\n')
            for i, (cp, data) in enumerate(sorted(wide)):
                hex_data = ', '.join(f'0x{b:02X}' for b in data)
                f.write(f'    {{{hex_data}}},  /* [{i}] U+{cp:04X} */\n')
            f.write('};\n\n')

            f.write(f'const uint32_t {name}_wide_index[] = {{\n')
            for i, (cp, _) in enumerate(sorted(wide)):
                f.write(f'    0x{cp:04X},\n')
            f.write('};\n')
            f.write(f'const uint32_t {name}_wide_count = {len(wide)};\n')


def generate_binary(glyphs, output_path):
    """Generate binary font file for CJK glyphs."""
    # Only include wide (16x16) glyphs for CJK
    wide = [(cp, data) for cp, w, data in glyphs if w > 8]
    wide.sort(key=lambda x: x[0])

    if not wide:
        print('No wide glyphs found!', file=sys.stderr)
        return

    glyph_count = len(wide)
    glyph_width = 16
    glyph_height = 16
    bytes_per_glyph = 32

    with open(output_path, 'wb') as f:
        # Write header
        # struct font_file_header {
        #     uint32_t magic;
        #     uint16_t version;
        #     uint16_t glyph_count;
        #     uint8_t  glyph_width;
        #     uint8_t  glyph_height;
        #     uint8_t  bytes_per_glyph;
        #     uint8_t  reserved;
        # };
        header = struct.pack('<IHHBBBB',
                             FONT_MAGIC,
                             1,  # version
                             glyph_count,
                             glyph_width,
                             glyph_height,
                             bytes_per_glyph,
                             0)  # reserved
        f.write(header)

        # Write index (codepoint array)
        for cp, _ in wide:
            f.write(struct.pack('<I', cp))

        # Write glyph data
        for _, data in wide:
            f.write(data)

    print(f'Generated binary font: {output_path}', file=sys.stderr)
    print(f'  Glyphs: {glyph_count}', file=sys.stderr)
    print(f'  Size: {12 + glyph_count * 4 + glyph_count * bytes_per_glyph} bytes', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description='Convert BDF font to C arrays or binary')
    parser.add_argument('input', help='Input BDF file')
    parser.add_argument('-o', '--output', required=True, help='Output file')
    parser.add_argument('--name', default='font', help='C array name prefix')
    parser.add_argument('--ascii', action='store_true', help='ASCII (0x00-0x7F)')
    parser.add_argument('--cjk', action='store_true', help='CJK Unified (U+4E00-U+9FFF)')
    parser.add_argument('--cjk-common', action='store_true', help='Common CJK subset (~3000 chars)')
    parser.add_argument('--range', action='append', help='Custom range (e.g., 0x4E00-0x9FFF)')
    parser.add_argument('--binary', action='store_true', help='Output binary format instead of C')

    args = parser.parse_args()

    ranges = []
    if args.ascii:
        ranges.append((0x00, 0x7F))
    if args.cjk:
        ranges.append((0x4E00, 0x9FFF))
    if args.cjk_common:
        # Common CJK characters (most frequently used)
        # This is a subset that covers most common Chinese text
        ranges.append((0x4E00, 0x9FA5))  # CJK Unified Ideographs
    if args.range:
        for r in args.range:
            m = re.match(r'(0x[0-9A-Fa-f]+)-(0x[0-9A-Fa-f]+)', r)
            if m:
                ranges.append((int(m.group(1), 16), int(m.group(2), 16)))

    if not ranges:
        ranges.append((0x00, 0x7F))

    def codepoint_filter(cp):
        return any(start <= cp <= end for start, end in ranges)

    print(f'Parsing {args.input}...', file=sys.stderr)
    glyphs = list(parse_bdf(args.input, codepoint_filter))
    print(f'Found {len(glyphs)} glyphs', file=sys.stderr)

    if args.binary:
        generate_binary(glyphs, args.output)
    else:
        generate_c_array(glyphs, args.name, args.output)
        print(f'Generated: {args.output}', file=sys.stderr)


if __name__ == '__main__':
    main()
