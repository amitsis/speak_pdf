#!/usr/bin/env python3

# $Id: speakpdf.py,v 1.1 2026/06/14 17:02:12 mitsis Exp $
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

from openai import OpenAI

# ----------------------------
# 1. CONFIGURATION
# ----------------------------
# Securely retrieve the OpenAI API key from the environment.
# The script refuses to start if the required credential is missing.
OPENAI_KEY = os.getenv("OPENAI_API_KEY")

if not OPENAI_KEY:
    raise RuntimeError(
        "CRITICAL: Missing API key (OPENAI_API_KEY) in environment."
    )

client = OpenAI(api_key=OPENAI_KEY)

# ----------------------------
# 2. CONSTANTS
# ----------------------------
# OpenAI TTS input has a practical per-request input limit.
# OCR-extracted PDF text can be very long, so the script splits
# the input into safe chunks and synthesizes each chunk separately.
DEFAULT_CHUNK_CHARS = 5500
DEFAULT_OUTPUT = "speech.opus"
DEFAULT_TEXT_EXPORT = "extracted.txt"


# ----------------------------
# 3. UTILITY FUNCTIONS
# ----------------------------
def normalize_text(text: str) -> str:
    """Clean OCR/PDF text enough for natural TTS reading."""
    text = text.replace("\x00", "")
    text = text.replace("\r\n", "\n").replace("\r", "\n")

    # Remove common line-break hyphenation from OCR/PDF text.
    text = re.sub(r"(\w)-\n(\w)", r"\1\2", text)

    # Collapse excessive blank lines while keeping paragraph boundaries.
    text = re.sub(r"[ \t]+", " ", text)
    text = re.sub(r"\n{3,}", "\n\n", text)

    return text.strip()


def read_text_file(path: Path) -> str:
    """Read a normal text/ASCII file as UTF-8 with tolerant decoding."""
    return path.read_text(encoding="utf-8", errors="replace")


def extract_pdf_with_pypdf(path: Path, pages: Optional[Tuple[int, int]] = None) -> str:
    """Extract text from a PDF text layer using pypdf if available."""
    try:
        from pypdf import PdfReader
    except ImportError as exc:
        raise RuntimeError(
            "Python package 'pypdf' is not installed. Install it with: pip install pypdf"
        ) from exc

    reader = PdfReader(str(path))
    total_pages = len(reader.pages)
    start, end = page_range_to_indexes(pages, total_pages)

    extracted: List[str] = []
    for index in range(start, end):
        page_text = reader.pages[index].extract_text() or ""
        if page_text.strip():
            extracted.append(f"\n\n--- Page {index + 1} ---\n\n{page_text}")

    return "\n".join(extracted)


def extract_pdf_with_pdftotext(path: Path, pages: Optional[Tuple[int, int]] = None) -> str:
    """Extract text from a PDF text layer using Poppler's pdftotext."""
    if not shutil.which("pdftotext"):
        raise RuntimeError(
            "Neither pypdf extraction nor 'pdftotext' is available. "
            "Install one of them: pip install pypdf  OR  sudo zypper install poppler-tools"
        )

    cmd = ["pdftotext", "-layout"]
    if pages:
        cmd.extend(["-f", str(pages[0]), "-l", str(pages[1])])
    cmd.extend([str(path), "-"])

    result = subprocess.run(
        cmd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.stdout


def extract_pdf_text(path: Path, pages: Optional[Tuple[int, int]] = None) -> str:
    """Extract selectable/OCR text from a PDF, preferring pypdf, falling back to pdftotext."""
    try:
        text = extract_pdf_with_pypdf(path, pages)
    except Exception as pypdf_error:
        print(f"pypdf extraction unavailable/failed: {pypdf_error}")
        print("Trying pdftotext fallback...")
        text = extract_pdf_with_pdftotext(path, pages)

    text = normalize_text(text)
    if not text:
        raise RuntimeError(
            "No readable text was extracted from the PDF. "
            "Make sure the PDF has an OCR text layer, e.g. created with OCRmyPDF."
        )
    return text


def parse_pages(value: Optional[str]) -> Optional[Tuple[int, int]]:
    """Parse page ranges like '3', '3-7', or '3:7' as 1-based inclusive ranges."""
    if not value:
        return None

    value = value.strip()
    match = re.fullmatch(r"(\d+)(?:\s*[-:]\s*(\d+))?", value)
    if not match:
        raise argparse.ArgumentTypeError("Use page syntax such as 3 or 3-7.")

    start = int(match.group(1))
    end = int(match.group(2) or start)
    if start < 1 or end < start:
        raise argparse.ArgumentTypeError("Page ranges must be positive and ascending.")
    return start, end


def page_range_to_indexes(pages: Optional[Tuple[int, int]], total_pages: int) -> Tuple[int, int]:
    """Convert a 1-based inclusive page range to Python's 0-based half-open indexes."""
    if not pages:
        return 0, total_pages

    start, end = pages
    if start > total_pages:
        raise RuntimeError(f"Page range starts after the end of the PDF ({total_pages} pages).")

    end = min(end, total_pages)
    return start - 1, end


def split_text(text: str, max_chars: int) -> List[str]:
    """Split long text into TTS-safe chunks, preferably at paragraph/sentence boundaries."""
    text = normalize_text(text)
    if len(text) <= max_chars:
        return [text]

    paragraphs = re.split(r"\n\s*\n", text)
    chunks: List[str] = []
    current = ""

    def flush_current() -> None:
        nonlocal current
        if current.strip():
            chunks.append(current.strip())
            current = ""

    for paragraph in paragraphs:
        paragraph = paragraph.strip()
        if not paragraph:
            continue

        if len(paragraph) > max_chars:
            flush_current()
            sentences = re.split(r"(?<=[.!?;:])\s+", paragraph)
            sentence_buffer = ""
            for sentence in sentences:
                if len(sentence) > max_chars:
                    if sentence_buffer.strip():
                        chunks.append(sentence_buffer.strip())
                        sentence_buffer = ""
                    for i in range(0, len(sentence), max_chars):
                        chunks.append(sentence[i:i + max_chars].strip())
                elif len(sentence_buffer) + len(sentence) + 1 <= max_chars:
                    sentence_buffer = f"{sentence_buffer} {sentence}".strip()
                else:
                    chunks.append(sentence_buffer.strip())
                    sentence_buffer = sentence
            if sentence_buffer.strip():
                chunks.append(sentence_buffer.strip())
            continue

        if len(current) + len(paragraph) + 2 <= max_chars:
            current = f"{current}\n\n{paragraph}".strip()
        else:
            flush_current()
            current = paragraph

    flush_current()
    return [chunk for chunk in chunks if chunk]


def print_progress(downloaded: int, estimated_total: int, bar_len: int = 40) -> None:
    """Print an approximate percentage progress bar for streamed audio bytes."""
    percent = min(downloaded / estimated_total, 1.0)
    filled = int(percent * bar_len)
    bar = "█" * filled + "░" * (bar_len - filled)
    print(f"\r[{bar}] {percent * 100:6.1f}%", end="", flush=True)


def synthesize_chunk(text: str, output_file: Path, model: str, voice: str) -> None:
    """Generate one OPUS audio file from one text chunk."""
    with client.audio.speech.with_streaming_response.create(
        model=model,
        voice=voice,
        input=text,
        response_format="opus",
    ) as response:
        downloaded = 0
        estimated_total = max(len(text.encode("utf-8")) * 18, 128 * 1024)

        with open(output_file, "wb") as f:
            for data in response.iter_bytes(chunk_size=65536):
                f.write(data)
                downloaded += len(data)
                print_progress(downloaded, estimated_total)

    print()


def concatenate_opus(parts: List[Path], output_file: Path) -> None:
    """Concatenate several OPUS parts into one OPUS file using ffmpeg."""
    if len(parts) == 1:
        output_file.write_bytes(parts[0].read_bytes())
        return

    if not shutil.which("ffmpeg"):
        raise RuntimeError(
            "Long input was split into several audio parts, but ffmpeg is not installed. "
            "Install it with: sudo zypper install ffmpeg"
        )

    with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False, suffix=".txt") as list_file:
        list_path = Path(list_file.name)
        for part in parts:
            list_file.write(f"file '{part.resolve()}'\n")

    try:
        subprocess.run(
            ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(list_path), "-c", "copy", str(output_file)],
            check=True,
        )
    finally:
        list_path.unlink(missing_ok=True)


def maybe_play(output_file: Path, no_play: bool) -> None:
    """Launch mpv in the background unless playback was disabled."""
    if no_play:
        return

    try:
        subprocess.Popen(
            ["mpv", str(output_file)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except FileNotFoundError:
        print("mpv not found. Install it with: sudo zypper install mpv")


# ----------------------------
# 4. COMMAND-LINE PROCESSING
# ----------------------------
def build_parser() -> argparse.ArgumentParser:
    """Define the command-line interface."""
    parser = argparse.ArgumentParser(
        description=(
            "Read text aloud from a direct string, a text/ASCII file, or an OCRmyPDF-processed PDF. "
            "For PDFs, the script reads the selectable OCR text layer."
        )
    )
    parser.add_argument(
        "input",
        help="PDF file, text/ASCII file, or direct text string to be spoken.",
    )
    parser.add_argument(
        "-o", "--output",
        default=DEFAULT_OUTPUT,
        help=f"Output OPUS audio file. Default: {DEFAULT_OUTPUT}",
    )
    parser.add_argument(
        "--voice",
        default="onyx",
        help="OpenAI TTS voice. Default: onyx",
    )
    parser.add_argument(
        "--model",
        default="gpt-4o-mini-tts",
        help="OpenAI TTS model. Default: gpt-4o-mini-tts",
    )
    parser.add_argument(
        "--pages",
        type=parse_pages,
        help="PDF page range to read, e.g. 1, 3-7, or 10:20. Uses 1-based page numbers.",
    )
    parser.add_argument(
        "--export-text",
        nargs="?",
        const=DEFAULT_TEXT_EXPORT,
        help=f"Export extracted text to a UTF-8 text file. Default name: {DEFAULT_TEXT_EXPORT}",
    )
    parser.add_argument(
        "--chunk-chars",
        type=int,
        default=DEFAULT_CHUNK_CHARS,
        help=f"Maximum characters per TTS request. Default: {DEFAULT_CHUNK_CHARS}",
    )
    parser.add_argument(
        "--no-play",
        action="store_true",
        help="Do not start automatic playback with mpv after generation.",
    )
    return parser


# ----------------------------
# 5. INPUT LOADING
# ----------------------------
def load_input(argument: str, pages: Optional[Tuple[int, int]]) -> str:
    """Load text from PDF, text file, or direct command-line string."""
    path = Path(argument)

    if path.is_file() and path.suffix.lower() == ".pdf":
        print(f"Extracting readable OCR/PDF text layer from: {path}")
        return extract_pdf_text(path, pages)

    if path.is_file():
        print(f"Reading text file: {path}")
        return normalize_text(read_text_file(path))

    return normalize_text(argument)


# ----------------------------
# 6. MAIN PROGRAM
# ----------------------------
def main() -> int:
    """Run extraction, optional text export, TTS generation, concatenation, and playback."""
    parser = build_parser()
    args = parser.parse_args()

    if args.chunk_chars < 1000:
        raise RuntimeError("--chunk-chars is too small. Use at least 1000.")

    analyse_text = load_input(args.input, args.pages)

    if args.export_text:
        export_path = Path(args.export_text)
        export_path.write_text(analyse_text + "\n", encoding="utf-8")
        print(f"Extracted text exported to: {export_path}")

    chunks = split_text(analyse_text, args.chunk_chars)
    output_file = Path(args.output)

    print(f"Text length: {len(analyse_text):,} characters")
    print(f"TTS chunks:  {len(chunks)}")
    print("Generating OPUS audio...")

    with tempfile.TemporaryDirectory(prefix="speech-parts-") as tmpdir:
        tmp_path = Path(tmpdir)
        part_files: List[Path] = []

        for number, chunk in enumerate(chunks, start=1):
            part_file = tmp_path / f"part-{number:04d}.opus"
            part_files.append(part_file)
            print(f"Chunk {number}/{len(chunks)} ({len(chunk):,} characters)")
            synthesize_chunk(chunk, part_file, args.model, args.voice)

        print("Combining OPUS audio parts...")
        concatenate_opus(part_files, output_file)

    print("Audio generation complete.")
    print(f"Audio saved to: {output_file}")

    maybe_play(output_file, args.no_play)
    return 0


if __name__ == "__main__":
    sys.exit(main())
