## 0.1.0

- Initial release of `codec_g723_1`.
- Pure C ITU-T G.723.1 speech codec backend with Dart Native Assets.
- Dual bitrate support: 6.3 kbps (MP-MLQ) and 5.3 kbps (ACELP).
- Support for standard 8 kHz narrowband PCM and wideband 16 kHz PCM (internal high-quality polyphase resampler).
- High-level `G723FileConverter` for converting WAV and G.723.1 bitstream files in background Dart isolates.
- Chunked streaming I/O with constant low memory footprint (<5 MB) even for multi-hour audio files.
- Live progress reporting via `SendPort` and instant cancellation via `G723CancellationToken`.
- Comprehensive streaming WAV reader and canonical WAV writer (`WavHeader`, `WavAudio`).
