#!/bin/sh
set -eu
python3 -m py_compile src/speak_pdf.py
gzip -t man/speak_pdf.1.gz
grep -q '^\.TH SPEAK_PDF 1' man/speak_pdf.1
echo OK
