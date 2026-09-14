# codec_g723_1 (Python Bindings)

Python ctypes bindings for the high-performance **ITU-T G.723.1** speech codec.

## Requirements

- Python 3.8+
- The compiled `g723_1` shared library (`libg723_1.so` on Linux, `libg723_1.dylib` on macOS, or `g723_1.dll` on Windows).

## Building the C Library

From the repository root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The Python module will automatically find the compiled shared library in `build/` or you can point `G723_LIB_PATH` to it:

```bash
export G723_LIB_PATH=/path/to/libg723_1.so
```

## Quick Start

### 1. Converting WAV Files to G.723.1 and Back

```python
from codec_g723_1 import encode_wav_to_g723, decode_g723_to_wav, G723Bitrate

# Encode 16-bit mono 8 kHz WAV to 6.3 kbps G.723.1 bitstream
frames = encode_wav_to_g723("input.wav", "encoded.g723", bitrate=G723Bitrate.KBPS_63)
print(f"Encoded {frames} frames.")

# Decode G.723.1 bitstream back to 8 kHz WAV
frames = decode_g723_to_wav("encoded.g723", "decoded.wav", sample_rate=8000)
print(f"Decoded {frames} frames.")
```

### 2. Streaming / Frame-Level Audio Processing

```python
import array
from codec_g723_1 import G723Encoder, G723Decoder, G723Bitrate

# 8000 Hz: 240 samples per frame (30 ms)
# 16000 Hz: 480 samples per frame (30 ms)
with G723Encoder(bitrate=G723Bitrate.KBPS_63, sample_rate=8000) as encoder, \
     G723Decoder(sample_rate=8000) as decoder:

    # 240 signed 16-bit PCM samples
    pcm_frame = array.array('h', [0] * 240)

    # Returns 24 bytes (or 20 bytes for 5.3 kbps)
    packet = encoder.encode_frame(pcm_frame)

    # Decode back to 480 raw PCM bytes (240 16-bit samples)
    decoded_pcm_bytes = decoder.decode_frame(packet)
```

## License

MIT License. See root `LICENSE` and `THIRD_PARTY_LICENSE` for details.
