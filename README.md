# codec_g723_1

A portable, high-performance **ITU-T G.723.1** speech codec implemented in **C99**, with first-class **Flutter / Dart** and **Python** bindings.

Supports both standardized G.723.1 bitrates (6.3 kbps and 5.3 kbps), narrowband 8 kHz and wideband 16 kHz PCM, chunked streaming file conversions with bounded memory footprint (<5 MB), and non-blocking background isolate execution.

---

## Repository Structure

```text
codec_g723_1/
├── CMakeLists.txt             # Standalone C library build (shared + static + tests)
├── include/                   # Public C API headers
│   └── codec_g723_1.h
├── src/                       # Core C99 DSP codec implementation
│   ├── codec_g723_1.c         # Dispatch, API entry points, and resampler wrapper
│   └── backends/c/            # ITU-T G.723.1 DSP algorithms (MP-MLQ & ACELP)
├── lib/                       # Dart / Flutter package API
│   ├── codec_g723_1.dart      # Root library export
│   └── src/
│       ├── converter.dart     # Isolate-backed streaming file converter
│       └── wav_file.dart      # Streaming WAV reader/writer utilities
├── hook/                      # Dart Native Assets hook (compiles C automatically)
│   └── build.dart
├── python/                    # Python package and bindings (ctypes)
│   ├── pyproject.toml
│   ├── codec_g723_1/          # Pythonic wrapper module
│   └── tests/                 # Python unit tests
└── example/                   # Complete Flutter demo application
```

---

## Features

- **Portable C99 Library**: Self-contained with zero external dependencies beyond standard C runtime math (`-lm`). Compiles with GCC, Clang, or MSVC.
- **CMake Build System**: Ready to build as shared (`.so`, `.dylib`, `.dll`) and static (`.a`, `.lib`) libraries, or integrate directly via `add_subdirectory()`.
- **Flutter & Dart Native Assets**: Compiles C automatically during `flutter run` / `flutter build` without manual toolchain or FFI setup.
- **Python Bindings (ctypes)**: Pure Python standard library bindings with zero pip runtime dependencies.
- **Dual Bitrate Modes**:
  - **6.3 kbps (MP-MLQ)**: High-quality speech coding (24 bytes / 30 ms frame).
  - **5.3 kbps (ACELP)**: Maximum bandwidth compression (20 bytes / 30 ms frame).
- **8 kHz & 16 kHz Audio Support**:
  - Native 8 kHz 16-bit mono PCM (240 samples / frame).
  - 16 kHz 16-bit mono/stereo PCM (480 samples / frame) via transparent internal polyphase resampler.
- **Background Isolate File Conversion (`G723FileConverter`)**:
  - Offloads heavy audio processing entirely from the UI thread to a background Dart isolate.
  - Non-blocking 60/120 FPS UI with real-time progress reporting and cancellation.
- **Streaming Chunked I/O**:
  - Memory usage is bounded (<5 MB) even when converting multi-hour audio files.

---

## 1. Using in C / C++ Projects

### Building with CMake

```bash
# Configure and build shared & static libraries + C tests
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run C test suite
ctest --test-dir build --output-on-failure
```

### Integrating in Your Own CMakeLists.txt

Add `codec_g723_1` as a submodule or subfolder:

```cmake
add_subdirectory(codec_g723_1)

# Link against the shared library
target_link_libraries(my_application PRIVATE g723_1)

# Or link against the static library
# target_link_libraries(my_application PRIVATE g723_1_static)
```

### C API Example

```c
#include <stdio.h>
#include <codec_g723_1.h>

int main(void) {
    printf("G.723.1 ABI Version: %u, Backend: %s\n", 
           g723_abi_version(), g723_backend_name());

    // Create encoder for 6.3 kbps
    void *encoder = NULL;
    if (g723_encoder_create(G723_BITRATE_6300, &encoder) != 0) {
        return 1;
    }

    // 240 signed 16-bit PCM samples (30 ms at 8 kHz)
    int16_t pcm[240] = {0};
    uint8_t out_packet[G723_FRAME_BYTES_6300];
    uint32_t out_size = 0;

    // Encode frame (returns 24 bytes)
    g723_encode_frame(encoder, pcm, 240, out_packet, sizeof(out_packet), &out_size);
    printf("Encoded frame: %u bytes\n", out_size);

    g723_encoder_destroy(encoder);
    return 0;
}
```

---

## 2. Using in Python Projects

### Installation

Build the native library with CMake first:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Then install the Python package:

```bash
pip install ./python
```

*(The Python bindings automatically discover the compiled library in `build/`, `build_cmake/`, or via the `G723_LIB_PATH` environment variable).*

### Python Usage

```python
from codec_g723_1 import (
    G723Encoder, 
    G723Decoder, 
    G723Bitrate, 
    encode_wav_to_g723, 
    decode_g723_to_wav
)

# 1. Convert whole WAV files to G.723.1 bitstream and back
# Pass wav_header=True to wrap the G.723.1 bitstream in a RIFF WAVE container (format tag 0x0042)
encode_wav_to_g723("input.wav", "audio.g723", bitrate=G723Bitrate.KBPS_63, wav_header=False)

# Decoder automatically detects whether input is raw G.723.1 or wrapped in a WAV container!
decode_g723_to_wav("audio.g723", "output.wav", sample_rate=8000, wav_header=True)

# 2. Real-time frame processing
with G723Encoder(bitrate=G723Bitrate.KBPS_63, sample_rate=8000) as enc, \
     G723Decoder(sample_rate=8000) as dec:

    # 240 signed 16-bit integers
    pcm_in = [0] * 240

    # Returns 24 bytes (or 20 bytes for 5.3 kbps)
    packet = enc.encode_frame(pcm_in)

    # Decode back to raw 16-bit PCM bytes (480 bytes = 240 samples)
    pcm_out = dec.decode_frame(packet)
```

### Python CLI Demo (`codec_demo.py`)

A standalone CLI tool is included in `python/codec_demo.py`:

```bash
# Encode 8k/16k PCM WAV to raw 6.3 kbps G.723.1
python python/codec_demo.py --to6.3k input.wav

# Encode PCM WAV to 5.3 kbps inside a RIFF WAVE container (format tag 0x0042)
python python/codec_demo.py --to5.3k --wav input.wav

# Decode G.723.1 (raw or WAV container, auto-detected) to 8 kHz PCM WAV
python python/codec_demo.py --to8kpcm --wav input.g723

# Decode G.723.1 to raw 16-bit 8 kHz PCM (no WAV header)
python python/codec_demo.py --to8kpcm input.g723
```

To run the Python test suite:

```bash
python -m unittest discover -s python/tests
```

---

## 3. Using in Flutter & Dart Projects

### Installation

Add `codec_g723_1` to your `pubspec.yaml`:

```yaml
dependencies:
  codec_g723_1: ^0.1.0
```

Or reference the GitHub repository directly:

```yaml
dependencies:
  codec_g723_1:
    git:
      url: https://github.com/shabaz123/codec_g723_1.git
```

### Background File Conversion (Isolates & Streaming)

```dart
import 'package:codec_g723_1/codec_g723_1.dart';

Future<void> convertAudio() async {
  final cancellationToken = G723CancellationToken();

  final result = await G723FileConverter.convert(
    mode: G723ConversionMode.pcmToG723_63k,
    sourcePath: '/path/to/recording.wav',
    destinationPath: '/path/to/output.g723',
    cancellationToken: cancellationToken,
    onProgress: (progressFraction, statusText) {
      print('Progress: ${(progressFraction * 100).toInt()}% - $statusText');
    },
  );

  if (result.success) {
    print('Done! Compression ratio: ${result.compressionRatio.toStringAsFixed(2)}x');
  }
}
```

### Frame-Level Real-Time Audio (VoIP / Streaming)

```dart
import 'dart:typed_data';
import 'package:codec_g723_1/codec_g723_1.dart';

void main() {
  final encoder = G723Encoder(bitrate: G723Bitrate.kbps63, sampleRate: 8000);
  final decoder = G723Decoder(sampleRate: 8000);

  // 240 samples per frame at 8 kHz (30 ms)
  final pcmFrame = Int16List(240);

  // Encode -> exactly 24 bytes (or 20 bytes for 5.3k)
  final Uint8List packet = encoder.encodeFrame(pcmFrame);

  // Decode -> 240 PCM samples
  final Int16List decodedPcm = decoder.decodeFrame(packet);

  encoder.close();
  decoder.close();
}
```

---

## Codec Technical Specifications

| Parameter | 6.3 kbps Mode | 5.3 kbps Mode |
| :--- | :--- | :--- |
| **Algorithm** | MP-MLQ (Multi-Pulse MLQ) | ACELP (Algebraic CELP) |
| **Bitrate** | 6300 bps | 5300 bps |
| **Frame Duration** | 30 ms | 30 ms |
| **Narrowband Samples / Frame** | 240 samples (8 kHz) | 240 samples (8 kHz) |
| **Wideband Samples / Frame** | 480 samples (16 kHz) | 480 samples (16 kHz) |
| **Encoded Packet Size** | 24 bytes | 20 bytes |
| **Algorithmic Lookahead** | 7.5 ms (60 samples @ 8k) | 7.5 ms (60 samples @ 8k) |
| **Uncompressed PCM Bandwidth** | 128 kbps (16-bit @ 8 kHz) | 128 kbps (16-bit @ 8 kHz) |
| **Raw Compression Ratio** | ~20.3 : 1 | ~24.1 : 1 |

---

## Platform Support & Prerequisites

Currently, Flutter/Dart execution is **actively tested on Android** (both 32-bit `armeabi-v7a` and 64-bit `arm64-v8a` physical devices). 

Because the native backend is written in portable, self-contained **C99** and compiled via Dart's [Native Assets](https://dart.dev/interop/c-interop#native-assets) system (`package:native_toolchain_c`) and standard CMake, it is architected to compile across all major desktop and mobile targets:

| Platform | Verification Status | Toolchain Prerequisites | Build Mechanism |
| :--- | :--- | :--- | :--- |
| **Android** | **Tested & Verified** | Android NDK (via Android Studio / SDK Manager) | Built automatically by Gradle via NDK Clang |
| **iOS** | Untested | macOS with Xcode & Command Line Tools (`xcode-select --install`) | Automatically compiled into iOS App Framework via Apple Clang |
| **macOS** | Untested | macOS with Xcode or Command Line Tools | Compiled into `libcodec_g723_1.dylib` automatically |
| **Linux** | Untested | GCC or Clang (`sudo apt install build-essential clang`) | Compiled into `libcodec_g723_1.so` automatically |
| **Windows** | **Tested (C & Python)** | Visual Studio with "Desktop development with C++" or MinGW-w64 | Compiled into `g723_1.dll` automatically |
| **Web** | Unsupported | N/A (Requires Wasm compilation pipeline) | `dart:ffi` Native Assets is not supported on web |

---

## Example Flutter Application

A complete example Flutter application demonstrating the codec is included in the [`example/`](example/) directory:

- **Interactive File Converter**: Convert between PCM WAV (8 kHz / 16 kHz) and G.723.1 (5.3 kbps / 6.3 kbps), with optional WAV container wrapper (`WAVE_FORMAT_MSG723`).
- **Auto-Detecting Decoder**: Automatically reads both raw `.g723` bitstreams and RIFF `.wav` containers.
- **Built-in Test Signal Generator**: Synthesizes 8 kHz and 16 kHz harmonic audio samples with a single click to test without external audio files.
- **Non-blocking Background Isolates**: Conversion runs in background Dart isolates without freezing the UI thread, complete with real-time progress tracking and cancellation support.
- **Detailed Metrics Card**: Displays processed frames, duration, byte sizes, compression ratio, and frame breakdown.

### Running the Example App

> [!NOTE]
> **Do you need to build the C library first?**
> **No!** You do **not** need to run CMake or build any C libraries manually before running the Flutter app.
> Flutter utilizes Dart's **Native Assets** system ([`hook/build.dart`](hook/build.dart)). When you run `flutter run` or `flutter build apk`, Flutter automatically invokes the native toolchain (such as Android NDK Clang) to compile the C sources (`src/`) and bundles the resulting library (`libcodec_g723_1.so`) directly into the app package.

#### 1. Prerequisites (Android)
- **Flutter SDK** (3.27+).
- **Android SDK & NDK**: Ensure the Android NDK is installed via Android Studio (*Settings / Preferences > Appearance & Behavior > System Settings > Android SDK > SDK Tools > NDK (Side by side)*).
- An attached physical Android device (with USB debugging enabled) or an active Android Virtual Device (AVD).

#### 2. Get Dependencies & Launch
From the repository root:

```bash
cd example
flutter pub get
flutter run
```

#### 3. Clean & Rebuild (Troubleshooting)
If you switch devices, update C source files, or encounter cached build artifacts, clean the workspace and re-fetch dependencies:

```bash
cd example
flutter clean
flutter pub get
flutter run
```

---

## License & Third-Party Notice

This project is licensed under the **MIT License**. See [`LICENSE`](LICENSE) for details.

Portions of this software are derived from or based on [`oxideav-g7231`](https://github.com/OxideAV/oxideav-g7231), also licensed under the MIT License. See [`THIRD_PARTY_LICENSE`](THIRD_PARTY_LICENSE) for complete copyright notices.
