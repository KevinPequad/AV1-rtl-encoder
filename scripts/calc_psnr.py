#!/usr/bin/env python3
"""Calculate PSNR/SSIM between original YUV and decoded YUV using ffmpeg."""

import subprocess
import shutil
import sys
import os

def calc_psnr(original_yuv, decoded_yuv, width, height, fps=24):
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        print("ERROR: ffmpeg not found", file=sys.stderr)
        sys.exit(1)

    size_str = f'{width}x{height}'

    # PSNR
    cmd = [
        ffmpeg, '-y', '-hide_banner',
        '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-s', size_str, '-r', str(fps),
        '-i', original_yuv,
        '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-s', size_str, '-r', str(fps),
        '-i', decoded_yuv,
        '-lavfi', f'[0:v][1:v]psnr',
        '-f', 'null', '-',
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    for line in result.stderr.splitlines():
        if 'PSNR' in line or 'average' in line:
            print(line.strip())

    # SSIM
    cmd_ssim = [
        ffmpeg, '-y', '-hide_banner',
        '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-s', size_str, '-r', str(fps),
        '-i', original_yuv,
        '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-s', size_str, '-r', str(fps),
        '-i', decoded_yuv,
        '-lavfi', f'[0:v][1:v]ssim',
        '-f', 'null', '-',
    ]
    result = subprocess.run(cmd_ssim, capture_output=True, text=True)
    for line in result.stderr.splitlines():
        if 'SSIM' in line or 'All' in line:
            print(line.strip())

if __name__ == '__main__':
    if len(sys.argv) < 5:
        print(f"Usage: {sys.argv[0]} <original.yuv> <decoded.yuv> <width> <height> [fps]")
        sys.exit(1)
    orig = sys.argv[1]
    decoded = sys.argv[2]
    w = int(sys.argv[3])
    h = int(sys.argv[4])
    fps = int(sys.argv[5]) if len(sys.argv) > 5 else 24
    calc_psnr(orig, decoded, w, h, fps)
