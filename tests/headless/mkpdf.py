#!/usr/bin/env python3
# A small PDF written by hand for tests/headless/mupf.sh (no PDF library, nothing downloaded): two Letter pages of
# Helvetica text, a black box in the middle of the first (an ink probe), the word "lantern" three times on the second and
# once on the first, an outline of two entries ("Chapter One" to page 1, "Chapter Two" to page 2), and on page 1 a link
# to page 2. Usage: mkpdf.py OUT.pdf
import sys


def main(out):
    page1 = ('BT /F1 24 Tf 72 700 Td (First page of the test) Tj ET\n'
             'BT /F1 14 Tf 72 660 Td (A lantern on the first page.) Tj ET\n'
             'BT /F1 14 Tf 72 620 Td (Go to page two) Tj ET\n'
             '0 0 0 rg 206 296 200 200 re f\n')
    page2 = ('BT /F1 24 Tf 72 700 Td (Second page) Tj ET\n'
             'BT /F1 14 Tf 72 660 Td (One lantern, two lantern, three lantern.) Tj ET\n')
    objs = [
        '<< /Type /Catalog /Pages 2 0 R /Outlines 7 0 R /PageMode /UseOutlines >>',
        '<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>',
        '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 10 0 R >> >> /Contents 5 0 R '
        '/Annots [11 0 R] >>',
        '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 10 0 R >> >> /Contents 6 0 R >>',
        '<< /Length %d >>\nstream\n%sendstream' % (len(page1), page1),
        '<< /Length %d >>\nstream\n%sendstream' % (len(page2), page2),
        '<< /Type /Outlines /First 8 0 R /Last 9 0 R /Count 2 >>',
        '<< /Title (Chapter One) /Parent 7 0 R /Next 9 0 R /Dest [3 0 R /XYZ 0 792 0] >>',
        '<< /Title (Chapter Two) /Parent 7 0 R /Prev 8 0 R /Dest [4 0 R /XYZ 0 792 0] >>',
        '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
        '<< /Type /Annot /Subtype /Link /Rect [70 615 200 637] /Border [0 0 0] /Dest [4 0 R /XYZ 0 792 0] >>',
        '<< /Title (mupf test) >>',
    ]
    data = b'%PDF-1.4\n'
    offsets = []
    for i, o in enumerate(objs):
        offsets.append(len(data))
        data += ('%d 0 obj\n%s\nendobj\n' % (i + 1, o)).encode('latin-1')
    xref = len(data)
    data += ('xref\n0 %d\n0000000000 65535 f \n' % (len(objs) + 1)).encode()
    for off in offsets:
        data += ('%010d 00000 n \n' % off).encode()
    data += ('trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%d\n%%EOF\n'
             % (len(objs) + 1, len(objs), xref)).encode()
    with open(out, 'wb') as f:
        f.write(data)


if __name__ == '__main__':
    main(sys.argv[1])
