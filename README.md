# codec_g723_1

**ITU-T G.723.1** speech codec for **Flutter** and **Dart**, powered by Dart Native Assets and an C engine.

Supports both standard G.723.1 bitrates (6.3 kbps and 5.3 kbps), narrowband 8 kHz PCM and wideband 16 kHz PCM, chunked streaming file conversions with bounded memory footprint (<5 MB), and non-blocking background isolate execution with real-time progress updates and cancellation.

---

## Features

- **Pure C Engine with Dart Native Assets**: No complex build systems or external shared library dependencies to configure. The native engine compiles automatically with your Flutter/Dart application using `dart:ffi` Native Assets.
- **Dual Bitrate Support**:
  - **6.3 kbps (MP-MLQ)**: High-quality speech coding (24 bytes / 30 ms frame).
  - **5.3 kbps (ACELP)**: Maximum bandwidth compression (20 bytes / 30 ms frame).
- **8 kHz and 16 kHz Audio Support**:
  - Native 8 kHz 16-bit mono PCM (240 samples / frame).
  - 16 kHz 16-bit mono/stereo PCM (480 samples / frame) via an internal low-pass polyphase resampler.
- **Background Isolate File Conversion (`G723FileConverter`)**:
  - Offloads heavy audio processing entirely from the Flutter UI isolate to a background worker isolate.
  - Zero UI lag, 60/120 FPS fluid rendering, and zero Android ANR ("Application Not Responding") risk.
- **Streaming Chunked I/O**:
  - Processes files in streaming batches using `RandomAccessFile` without reading entire files into RAM.
  - Constant memory consumption (<5 MB) even for multi-hour audio files.
- **Progress Tracking & Cancellation**:
  - Real-time progress updates (`framesProcessed`, `totalFrames`, `progressFraction`) streamed over `SendPort`.
  - Clean, instant cancellation via `G723CancellationToken`.
- **WAV File Support (`WavHeader`, `WavAudio`)**:
  - Reads and writes canonical 16-bit PCM WAV files.
  - Automatic downmixing from stereo to mono.
  - Test speech signal generation for testing.
- **Cross-Platform**:
  - Android, iOS, Windows, macOS, and Linux.

---

## Installation

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

Then run:

```bash
flutter pub get
```

---

## Quick Start

### 1. Converting Files in Background Isolates

Use `G723FileConverter.convert` to convert files without blocking the UI thread:

```dart
import 'package:codec_g723_1/codec_g723_1.dart';

Future<void> convertAudio() async {
  final cancellationToken = G723CancellationToken();

  final result = await G723FileConverter.convert(
    mode: G723ConversionMode.pcmToG723_63k, // or pcmToG723_53k, g723ToPcm8k, etc.
    sourcePath: '/path/to/speech.wav',
    destinationPath: '/path/to/speech.g723',
    cancellationToken: cancellationToken,
    onProgress: (progressFraction, statusText) {
      print('Progress: ${(progressFraction * 100).toStringAsFixed(1)}% - $statusText');
    },
  );

  if (result.success) {
    print('Conversion succeeded!');
    print('Frames processed: ${result.framesProcessed}');
    print('Compression ratio: ${result.compressionRatio.toStringAsFixed(2)}x');
    print('Audio duration: ${result.durationSeconds.toStringAsFixed(1)}s');
  } else {
    print('Conversion failed: ${result.errorMessage}');
  }
}
```

To cancel an in-progress conversion (e.g. if the user navigates away or taps a Cancel button):

```dart
cancellationToken.cancel();
```

---

### 2. Decoding a G.723.1 File to WAV

```dart
final result = await G723FileConverter.convert(
  mode: G723ConversionMode.g723ToPcm8k, // Decodes bitstream to 8 kHz 16-bit Mono WAV
  sourcePath: '/path/to/recording.g723',
  destinationPath: '/path/to/decoded.wav',
  onProgress: (prog, msg) => print('$msg (${(prog * 100).toInt()}%)'),
);
```

---

### 3. Real-Time Frame-Level Encoding (VoIP / Streaming)

For real-time applications such as VoIP or live audio streaming, use `G723Encoder` directly:

```dart
import 'dart:typed_data';
import 'package:codec_g723_1/codec_g723_1.dart';

void main() {
  // Create an encoder for 6.3 kbps at 8 kHz
  final encoder = G723Encoder(
    bitrate: G723Bitrate.kbps63,
    sampleRate: 8000,
  );

  // Each frame requires exactly 240 samples at 8 kHz (30 ms of audio)
  final pcmFrame = Int16List(240);
  // ... fill pcmFrame with microphone samples ...

  // Returns exactly 24 bytes (or 20 bytes for 5.3 kbps)
  final Uint8List g723Packet = encoder.encodeFrame(pcmFrame);

  print('Encoded frame size: ${g723Packet.length} bytes');

  // Always close native resources when done
  encoder.close();
}
```

---

### 4. Real-Time Frame-Level Decoding

```dart
import 'dart:typed_data';
import 'package:codec_g723_1/codec_g723_1.dart';

void main() {
  final decoder = G723Decoder(sampleRate: 8000);

  // g723Packet is a 20-byte or 24-byte packet received from network or stream
  final Uint8List g723Packet = getIncomingPacket();

  // Inspect frame type
  final type = G723Codec.frameType(g723Packet);
  print('Received frame type: $type');

  // Decode to 240 PCM samples (30 ms of audio)
  final Int16List pcmSamples = decoder.decodeFrame(g723Packet);

  // ... feed pcmSamples to audio output buffer ...

  decoder.close();
}
```

---

### 5. In-Memory WAV Reading and Writing

```dart
import 'package:codec_g723_1/codec_g723_1.dart';

// Generate a test harmonic speech-like signal
final testSignal = WavAudio.generateTestSignal(sampleRate: 8000, durationSeconds: 3.0);

// Write to canonical 16-bit mono WAV bytes
final Uint8List wavBytes = WavAudio.writeWav(
  samples: testSignal.samples,
  sampleRate: 8000,
);

// Parse WAV bytes (automatically downmixes stereo to mono if needed)
final WavAudio parsed = WavAudio.readWav(wavBytes);
print('Duration: ${parsed.durationSeconds}s, samples: ${parsed.samples.length}');
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

## Example Flutter Application

A complete example Flutter application demonstrating the codec is included in the [`example/`](example/) directory:

- File pickers to choose input WAV or G.723.1 files.
- Built-in test signal generator (8 kHz and 16 kHz).
- Full conversion modes (PCM $\leftrightarrow$ G.723.1 and roundtrip).
- Live progress indicator, background worker isolate execution, and responsive UI.
- Detailed metrics card displaying processed frames, compression ratio, byte sizes, and frame type distribution.

To run the example app on an attached device:

```bash
cd example
flutter run
```

---

## Platform Support & Prerequisites

Currently, this package is **actively tested on Android** (both 32-bit `armeabi-v7a` and 64-bit `arm64-v8a` physical devices). 

Because the native backend is written in portable, self-contained **C99** and compiled via Dart's [Native Assets](https://dart.dev/interop/c-interop#native-assets) system (`package:native_toolchain_c`), it is architected to compile across all major desktop and mobile targets. Here is what is needed for each platform:

| Platform | Verification Status | Toolchain Prerequisites | Build Mechanism |
| :--- | :--- | :--- | :--- |
| **Android** | **Tested & Verified** | Android NDK (installed via Android Studio / SDK Manager) | Built automatically by Gradle & Flutter via NDK Clang |
| **iOS** | Untested | macOS with Xcode & Command Line Tools (`xcode-select --install`) | Automatically compiled into the iOS App Framework via Apple Clang |
| **macOS** | Untested | macOS with Xcode or Command Line Tools | Compiled into `libcodec_g723_1.dylib` automatically |
| **Linux** | Untested | GCC or Clang (`sudo apt install build-essential clang`) | Compiled into `libcodec_g723_1.so` automatically |
| **Windows** | Untested (host test passed) | Visual Studio with "Desktop development with C++" or MinGW-w64 | Compiled into `codec_g723_1.dll` automatically |
| **Web** | Unsupported | N/A (Requires Wasm compilation pipeline) | `dart:ffi` Native Assets is not supported on web |

### Building for Other Platforms

1. **iOS / macOS**:
   - Run `flutter run -d ios` or `flutter run -d macos` from a machine running macOS.
   - Ensure CocoaPods and Xcode are configured. Native Assets handles the C compilation step transparently during the build.
2. **Linux**:
   - Ensure a C toolchain is installed (`build-essential` on Ubuntu/Debian).
   - Run `flutter run -d linux`.
3. **Windows**:
   - Ensure Visual Studio C++ Build Tools (MSVC `cl.exe`) or MinGW-w64 GCC is in your system `PATH`.
   - Run `flutter run -d windows`.

> [!NOTE]
> The C code has zero external library dependencies other than standard C runtime math (`-lm`). If you test on iOS, macOS, Windows, or Linux, please open an issue or pull request with your results!

---

## License & Third-Party Notice

This project is licensed under the **MIT License**. See [`LICENSE`](LICENSE) for details.

Portions of this software are derived from or based on [`oxideav-g7231`](https://github.com/OxideAV/oxideav-g7231), also licensed under the MIT License. See [`THIRD_PARTY_LICENSE`](THIRD_PARTY_LICENSE) for complete copyright notices.

