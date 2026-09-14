#!/usr/bin/env python3
"""ITU-T G.723.1 Speech Codec CLI Demo.

Examples:
    # Encode PCM WAV to 6.3 kbps raw bitstream
    python codec_demo.py --to6.3k input.wav

    # Encode PCM WAV to 5.3 kbps inside a WAV container (WAVE_FORMAT_MSG723)
    python codec_demo.py --to5.3k --wav input.wav

    # Decode G.723.1 (raw or WAV container) to 8 kHz WAV
    python codec_demo.py --to8kpcm --wav input.g723

    # Decode G.723.1 to raw 16-bit 8 kHz PCM
    python codec_demo.py --to8kpcm input.g723
"""

import argparse
import os
import sys
import time

# Ensure package is importable if executed from any directory
script_dir = os.path.dirname(os.path.abspath(__file__))
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)

from codec_g723_1 import (
    G723Bitrate,
    encode_wav_to_g723,
    decode_g723_to_wav,
    get_abi_version,
    get_backend_name,
)


def format_bytes(size: int) -> str:
    if size < 1024:
        return f"{size} B"
    elif size < 1024 * 1024:
        return f"{size / 1024:.1f} KB"
    else:
        return f"{size / (1024 * 1024):.2f} MB"


def main():
    parser = argparse.ArgumentParser(
        prog="codec_demo.py",
        description="ITU-T G.723.1 Audio Codec Demo CLI",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  codec_demo.py --to6.3k input.wav               # Encode to raw G.723.1 (6.3k)
  codec_demo.py --to5.3k --wav input.wav         # Encode to G.723.1 WAV container (5.3k)
  codec_demo.py --to8kpcm --wav input.g723       # Decode to 8 kHz PCM WAV
  codec_demo.py --to8kpcm input.g723             # Decode to raw 8 kHz 16-bit PCM
        """,
    )

    mode_group = parser.add_mutually_exclusive_group(required=True)
    mode_group.add_argument(
        "--to5.3k",
        dest="mode",
        action="store_const",
        const="5.3k",
        help="Encode PCM WAV (8k or 16k) to G.723.1 5.3 kbps",
    )
    mode_group.add_argument(
        "--to6.3k",
        dest="mode",
        action="store_const",
        const="6.3k",
        help="Encode PCM WAV (8k or 16k) to G.723.1 6.3 kbps",
    )
    mode_group.add_argument(
        "--to8kpcm",
        dest="mode",
        action="store_const",
        const="8kpcm",
        help="Decode G.723.1 bitstream (raw or WAV) to 8 kHz 16-bit PCM",
    )

    parser.add_argument(
        "--wav",
        dest="wav",
        action="store_true",
        default=False,
        help="Output with a WAV header (otherwise raw bitstream or raw PCM is output)",
    )
    parser.add_argument(
        "-o",
        "--output",
        dest="output",
        default=None,
        help="Output file path (defaults based on source name, mode, and --wav)",
    )
    parser.add_argument(
        "source",
        metavar="sourcefilename",
        help="Source audio file (WAV for encoding, raw or WAV for decoding)",
    )

    args = parser.parse_args()

    if not os.path.isfile(args.source):
        sys.stderr.write(f"Error: Source file not found: '{args.source}'\n")
        sys.exit(1)

    # Determine default destination filename if not specified
    base_name, _ = os.path.splitext(args.source)
    if args.output:
        dest_path = args.output
    else:
        if args.mode == "5.3k":
            ext = ".wav" if args.wav else ".g723"
            dest_path = f"{base_name}_5.3k{ext}"
        elif args.mode == "6.3k":
            ext = ".wav" if args.wav else ".g723"
            dest_path = f"{base_name}_6.3k{ext}"
        elif args.mode == "8kpcm":
            ext = ".wav" if args.wav else ".pcm"
            dest_path = f"{base_name}_8k{ext}"
        else:
            dest_path = f"{base_name}_out.bin"

    print(f"G.723.1 Codec Engine: {get_backend_name()} (ABI v{get_abi_version()})")
    print(f"Mode:         {args.mode}")
    print(f"WAV Header:   {'Enabled' if args.wav else 'Disabled (raw)'}")
    print(f"Source:       {args.source}")
    print(f"Destination:  {dest_path}")

    start_time = time.perf_counter()

    try:
        if args.mode == "5.3k":
            frames = encode_wav_to_g723(
                wav_path=args.source,
                g723_path=dest_path,
                bitrate=G723Bitrate.KBPS_53,
                wav_header=args.wav,
            )
        elif args.mode == "6.3k":
            frames = encode_wav_to_g723(
                wav_path=args.source,
                g723_path=dest_path,
                bitrate=G723Bitrate.KBPS_63,
                wav_header=args.wav,
            )
        elif args.mode == "8kpcm":
            frames = decode_g723_to_wav(
                g723_path=args.source,
                wav_path=dest_path,
                sample_rate=8000,
                wav_header=args.wav,
            )
        else:
            sys.stderr.write(f"Error: Unknown mode '{args.mode}'\n")
            sys.exit(1)

        elapsed = time.perf_counter() - start_time
        src_size = os.path.getsize(args.source)
        dst_size = os.path.getsize(dest_path)
        audio_dur = frames * 0.030

        print(f"\nConversion successful in {elapsed:.3f} s:")
        print(f"  Frames processed: {frames} ({audio_dur:.2f} s audio)")
        print(f"  Input size:       {format_bytes(src_size)}")
        print(f"  Output size:      {format_bytes(dst_size)}")
        if dst_size > 0 and src_size > 0:
            if "pcm" in args.mode:
                ratio = dst_size / src_size
                print(f"  Expansion ratio:  {ratio:.2f}x")
            else:
                ratio = src_size / dst_size
                print(f"  Compression ratio:{ratio:.2f}x")
        print(f"  Saved to:         {dest_path}")

    except Exception as e:
        sys.stderr.write(f"\nConversion error: {e}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
