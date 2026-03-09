#!/usr/bin/env python3
"""Package raw AV1 OBU bitstream into MP4/WebM container.

Takes the output of the Verilator AV1 encoder simulation (raw OBU byte
stream) and wraps it in a playable container.

Usage:
    python3 package_mp4.py [input.obu] [output.mp4] [--fps N] [--width W] [--height H]
"""

import argparse
import os
import shutil
import subprocess
import sys

def resolve_tool(name):
    tool = shutil.which(name)
    if tool:
        return tool

    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    candidate = os.path.join(repo_root, 'tools', 'ffmpeg', 'ffmpeg-7.0.2-amd64-static', name)
    if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
        return candidate

    env_name = 'FFMPEG_BIN' if name == 'ffmpeg' else 'FFPROBE_BIN'
    env_tool = os.environ.get(env_name)
    if env_tool and os.path.isfile(env_tool) and os.access(env_tool, os.X_OK):
        return env_tool

    return None


def package_mp4_ffmpeg(input_obu, output_mp4, fps=24, width=1280, height=720):
    """Package raw AV1 OBU stream into MP4 using ffmpeg."""
    ffmpeg = resolve_tool('ffmpeg')
    if not ffmpeg:
        raise FileNotFoundError('ffmpeg is not installed or not on PATH')

    cmd = [
        ffmpeg, '-y', '-hide_banner', '-loglevel', 'error',
        '-f', 'av1',
        '-r', str(fps),
        '-i', input_obu,
        '-c:v', 'copy',
        '-movflags', '+faststart',
        output_mp4,
    ]
    proc = subprocess.run(cmd, check=True, capture_output=True, text=True)
    if proc.stderr.strip():
        print('ffmpeg warnings:')
        print(proc.stderr.strip())

    return probe_mp4(output_mp4)


def package_webm_ffmpeg(input_obu, output_webm, fps=24):
    """Package raw AV1 OBU stream into WebM using ffmpeg."""
    ffmpeg = resolve_tool('ffmpeg')
    if not ffmpeg:
        raise FileNotFoundError('ffmpeg is not installed or not on PATH')

    cmd = [
        ffmpeg, '-y', '-hide_banner', '-loglevel', 'error',
        '-f', 'av1',
        '-r', str(fps),
        '-i', input_obu,
        '-c:v', 'copy',
        output_webm,
    ]
    proc = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return probe_mp4(output_webm)


def probe_mp4(path):
    """Return a small decode summary using ffprobe."""
    ffprobe = resolve_tool('ffprobe')
    if not ffprobe:
        return {}

    cmd = [
        ffprobe, '-v', 'error',
        '-select_streams', 'v:0',
        '-show_entries', 'stream=codec_name,width,height,avg_frame_rate,nb_frames',
        '-of', 'default=noprint_wrappers=1',
        path,
    ]
    proc = subprocess.run(cmd, check=True, capture_output=True, text=True)
    info = {}
    for line in proc.stdout.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            info[key] = value
    return info


def compare_with_ffmpeg_ref(rtl_output, ffmpeg_ref, raw_yuv, width, height, fps):
    """Compare RTL encoder output against ffmpeg reference using PSNR/SSIM."""
    ffmpeg = resolve_tool('ffmpeg')
    if not ffmpeg:
        print("WARNING: ffmpeg not available for comparison")
        return

    # Decode RTL output to YUV
    rtl_decoded = rtl_output.replace('.obu', '_decoded.yuv')
    cmd_decode = [
        ffmpeg, '-y', '-hide_banner', '-loglevel', 'error',
        '-f', 'av1', '-i', rtl_output,
        '-pix_fmt', 'yuv420p', '-f', 'rawvideo', rtl_decoded,
    ]

    try:
        subprocess.run(cmd_decode, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as e:
        print(f"ERROR decoding RTL output: {e.stderr}")
        return

    # Compute PSNR between original and RTL decoded
    cmd_psnr = [
        ffmpeg, '-y', '-hide_banner',
        '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-s', f'{width}x{height}',
        '-r', str(fps), '-i', raw_yuv,
        '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-s', f'{width}x{height}',
        '-r', str(fps), '-i', rtl_decoded,
        '-lavfi', 'psnr=stats_file=-',
        '-f', 'null', '-',
    ]

    try:
        result = subprocess.run(cmd_psnr, check=True, capture_output=True, text=True)
        print("PSNR comparison (RTL vs original):")
        for line in result.stderr.splitlines():
            if 'PSNR' in line or 'psnr' in line:
                print(f"  {line}")
    except subprocess.CalledProcessError:
        print("WARNING: PSNR comparison failed")


def main():
    parser = argparse.ArgumentParser(
        description='Package a raw AV1 OBU bitstream into an MP4 container.')
    parser.add_argument('input', nargs='?',
                        default='/workspace/output/encoded.obu',
                        help='Path to the raw AV1 OBU file')
    parser.add_argument('output', nargs='?',
                        default='/workspace/output/output.mp4',
                        help='Path for the output MP4 file')
    parser.add_argument('--fps', type=int, default=24)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--raw-yuv', default=None,
                        help='Path to raw YUV for PSNR comparison')
    parser.add_argument('--ffmpeg-ref', default=None,
                        help='Path to ffmpeg reference encode for comparison')
    args = parser.parse_args()

    print('AV1 to MP4 Packager')
    print(f'  Input:  {args.input}')
    print(f'  Output: {args.output}')
    print(f'  Resolution: {args.width}x{args.height} @ {args.fps} fps')
    print()

    if not os.path.isfile(args.input):
        print(f'ERROR: input file not found: {args.input}', file=sys.stderr)
        sys.exit(1)

    file_size = os.path.getsize(args.input)
    if file_size == 0:
        print('ERROR: input file is empty', file=sys.stderr)
        sys.exit(1)

    print(f'Reading AV1 bitstream ({file_size} bytes) ...')
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    # Try MP4 first, then WebM
    try:
        print('Packaging as MP4...')
        info = package_mp4_ffmpeg(args.input, args.output,
                                  fps=args.fps, width=args.width, height=args.height)
        if info:
            print('ffprobe summary:')
            for key in ('codec_name', 'width', 'height', 'avg_frame_rate', 'nb_frames'):
                if key in info:
                    print(f'  {key}: {info[key]}')
    except Exception as e:
        print(f'MP4 packaging failed ({e}), trying WebM...')
        webm_output = args.output.rsplit('.', 1)[0] + '.webm'
        package_webm_ffmpeg(args.input, webm_output, fps=args.fps)

    # Compare with ffmpeg reference if available
    if args.raw_yuv:
        compare_with_ffmpeg_ref(args.input, args.ffmpeg_ref, args.raw_yuv,
                                args.width, args.height, args.fps)

    out_size = os.path.getsize(args.output)
    print(f'\nDone.  {args.output}  ({out_size} bytes)')


if __name__ == '__main__':
    main()
