#!/usr/bin/env python3
"""Render a CorridorKey alpha cutout onto a flat chroma screen so OBS Studio's
built-in Chroma Key filter can key it in real time (that filter is a GPU
pixel-shader pass inside OBS - no plugins, no custom shaders here).

Pipeline: infer.py --outputs alpha fg cutout  ->  cutout_*.mov (ProRes yuva)
            |
        this tool composites it over pure #00FF00 / #0000FF / #FF0000
            |
        OBS Media Source + stock "Chroma Key" filter, which keys a flat
        synthetic screen far more reliably than a lit fabric one.

Requires only ffmpeg/ffprobe on PATH (already required by this repo).
"""
import argparse
import subprocess


def probe(video):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select", "v:0",
         "-show_entries", "stream=width,height,r_frame_rate",
         "-of", "csv=p=0", video],
        capture_output=True, text=True, check=True).stdout.strip()
    w, h, fps = out.split(",")
    return w, h, fps


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--input", required=True,
                    help="CorridorKey cutout video (straight-alpha ProRes).")
    ap.add_argument("--screen", choices=("green", "blue", "red"), default="green",
                    help="Flat key color to composite onto (match the OBS filter).")
    ap.add_argument("--out", required=True, help="Output file for OBS Media Source.")
    ap.add_argument("--codec", default="libx264", help="Output codec (default libx264).")
    ap.add_argument("--crf", type=int, default=12, help="Quality (lower = better).")
    ap.add_argument("--pix_fmt", default="yuv444p",
                    help="8-bit yuv444p is safest for OBS; yuv444p10le also works.")
    args = ap.parse_args()

    hexc = {"green": "0x00ff00", "blue": "0x0000ff", "red": "0xff0000"}[args.screen]
    w, h, fps = probe(args.input)

    cmd = [
        "ffmpeg", "-y",
        "-f", "lavfi", "-i", f"color={hexc}:s={w}x{h}:r={fps}",
        "-i", args.input,
        "-filter_complex", "[0:v][1:v]overlay=0:0:format=auto",
        "-map_metadata", "-1",
        "-c:v", args.codec, "-pix_fmt", args.pix_fmt,
        "-crf", str(args.crf), "-threads", "2",
        args.out,
    ]
    subprocess.run(cmd, check=True)
    print(f"wrote {args.out}: {w}x{h} @{fps} flat {args.screen} screen")


if __name__ == "__main__":
    main()
