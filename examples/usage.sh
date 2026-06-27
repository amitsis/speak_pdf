#!/bin/sh
speak_pdf --pages 1-3 --export-text extracted.txt --no-play document.pdf
speak_pdf --voice nova -o lecture.opus lecture.pdf
speak_pdf "This text will be spoken aloud."
