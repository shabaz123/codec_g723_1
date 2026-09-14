import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';

import '../codec_g723_1.dart';

/// Available conversion modes for audio file conversion.
enum G723ConversionMode {
  pcmToG723_53k(
    label: 'PCM → G.723.1 (5.3 kbps)',
    description: 'Encode PCM WAV (8k or 16k) to G.723.1 5.3 kbps bitstream file',
    defaultExt: '.g723',
    sourceIsWav: true,
    destIsWav: false,
  ),
  pcmToG723_63k(
    label: 'PCM → G.723.1 (6.3 kbps)',
    description: 'Encode PCM WAV (8k or 16k) to G.723.1 6.3 kbps bitstream file',
    defaultExt: '.g723',
    sourceIsWav: true,
    destIsWav: false,
  ),
  g723ToPcm8k(
    label: 'G.723.1 → PCM 8 kHz (WAV)',
    description: 'Decode G.723.1 bitstream file to 8 kHz 16-bit Mono WAV',
    defaultExt: '.wav',
    sourceIsWav: false,
    destIsWav: true,
  ),
  g723ToPcm16k(
    label: 'G.723.1 → PCM 16 kHz (WAV)',
    description: 'Decode G.723.1 bitstream file to 16 kHz 16-bit Mono WAV',
    defaultExt: '.wav',
    sourceIsWav: false,
    destIsWav: true,
  ),
  roundtrip53k(
    label: 'PCM → G.723.1 5.3k → WAV (Roundtrip)',
    description: 'Encode PCM WAV to 5.3k and decode back to playable WAV',
    defaultExt: '.wav',
    sourceIsWav: true,
    destIsWav: true,
  ),
  roundtrip63k(
    label: 'PCM → G.723.1 6.3k → WAV (Roundtrip)',
    description: 'Encode PCM WAV to 6.3k and decode back to playable WAV',
    defaultExt: '.wav',
    sourceIsWav: true,
    destIsWav: true,
  );

  const G723ConversionMode({
    required this.label,
    required this.description,
    required this.defaultExt,
    required this.sourceIsWav,
    required this.destIsWav,
  });

  final String label;
  final String description;
  final String defaultExt;
  final bool sourceIsWav;
  final bool destIsWav;
}

/// The result of an audio conversion operation.
class G723ConversionResult {
  const G723ConversionResult({
    required this.success,
    this.errorMessage,
    required this.framesProcessed,
    required this.inputBytes,
    required this.outputBytes,
    required this.durationSeconds,
    required this.destinationPath,
    required this.frameTypeCounts,
    required this.compressionRatio,
  });

  final bool success;
  final String? errorMessage;
  final int framesProcessed;
  final int inputBytes;
  final int outputBytes;
  final double durationSeconds;
  final String destinationPath;
  final Map<String, int> frameTypeCounts;
  final double compressionRatio;
}

/// Token used to cancel an ongoing conversion task.
class G723CancellationToken {
  bool _isCancelled = false;
  final List<void Function()> _listeners = [];

  bool get isCancelled => _isCancelled;

  void cancel() {
    if (_isCancelled) return;
    _isCancelled = true;
    for (final listener in List.of(_listeners)) {
      listener();
    }
  }

  void _addListener(void Function() listener) {
    if (_isCancelled) {
      listener();
    } else {
      _listeners.add(listener);
    }
  }

  void _removeListener(void Function() listener) {
    _listeners.remove(listener);
  }
}

class _ConverterConfig {
  _ConverterConfig({
    required this.mode,
    required this.sourcePath,
    required this.destinationPath,
    required this.replyPort,
    this.progressPort,
  });

  final G723ConversionMode mode;
  final String sourcePath;
  final String destinationPath;
  final SendPort replyPort;
  final SendPort? progressPort;
}

class _ProgressMessage {
  const _ProgressMessage(this.progress, this.status);
  final double progress;
  final String status;
}

class _ErrorMessage {
  const _ErrorMessage(this.message);
  final String message;
}

/// High-performance audio file converter supporting streaming chunked I/O
/// and execution in background Dart isolates to prevent UI thread lockup.
abstract final class G723FileConverter {
  /// Converts an audio file in a background [Isolate] with streaming chunked I/O.
  ///
  /// Memory footprint is strictly bounded (<5 MB) regardless of audio file duration.
  /// Periodic progress updates are delivered via [onProgress].
  /// Ongoing conversions can be cancelled using [cancellationToken].
  static Future<G723ConversionResult> convert({
    required G723ConversionMode mode,
    required String sourcePath,
    required String destinationPath,
    void Function(double progress, String status)? onProgress,
    G723CancellationToken? cancellationToken,
  }) async {
    final sourceFile = File(sourcePath);
    if (!await sourceFile.exists()) {
      return G723ConversionResult(
        success: false,
        errorMessage: 'Source file does not exist: $sourcePath',
        framesProcessed: 0,
        inputBytes: 0,
        outputBytes: 0,
        durationSeconds: 0,
        destinationPath: destinationPath,
        frameTypeCounts: const {},
        compressionRatio: 0,
      );
    }

    if (cancellationToken != null && cancellationToken.isCancelled) {
      return G723ConversionResult(
        success: false,
        errorMessage: 'Conversion was cancelled before starting.',
        framesProcessed: 0,
        inputBytes: 0,
        outputBytes: 0,
        durationSeconds: 0,
        destinationPath: destinationPath,
        frameTypeCounts: const {},
        compressionRatio: 0,
      );
    }

    final replyPort = ReceivePort();
    ReceivePort? progressPort;
    SendPort? progressSendPort;

    if (onProgress != null) {
      progressPort = ReceivePort();
      progressSendPort = progressPort.sendPort;
      progressPort.listen((message) {
        if (message is _ProgressMessage) {
          onProgress(message.progress, message.status);
        }
      });
    }

    final config = _ConverterConfig(
      mode: mode,
      sourcePath: sourcePath,
      destinationPath: destinationPath,
      replyPort: replyPort.sendPort,
      progressPort: progressSendPort,
    );

    Isolate? isolate;
    void Function()? cancelCallback;

    try {
      isolate = await Isolate.spawn(
        _isolateWorker,
        config,
        errorsAreFatal: true,
      );

      bool isDone = false;

      if (cancellationToken != null) {
        cancelCallback = () {
          if (isDone) return;
          isolate?.kill(priority: Isolate.immediate);
          final partialFile = File(destinationPath);
          if (partialFile.existsSync()) {
            try {
              partialFile.deleteSync();
            } catch (_) {}
          }
          replyPort.sendPort.send(
            G723ConversionResult(
              success: false,
              errorMessage: 'Conversion cancelled by user.',
              framesProcessed: 0,
              inputBytes: 0,
              outputBytes: 0,
              durationSeconds: 0,
              destinationPath: destinationPath,
              frameTypeCounts: const {},
              compressionRatio: 0,
            ),
          );
        };
        cancellationToken._addListener(cancelCallback);
      }

      final response = await replyPort.first;
      isDone = true;

      if (response is G723ConversionResult) {
        return response;
      } else if (response is _ErrorMessage) {
        return G723ConversionResult(
          success: false,
          errorMessage: response.message,
          framesProcessed: 0,
          inputBytes: 0,
          outputBytes: 0,
          durationSeconds: 0,
          destinationPath: destinationPath,
          frameTypeCounts: const {},
          compressionRatio: 0,
        );
      } else {
        return G723ConversionResult(
          success: false,
          errorMessage: 'Unexpected worker isolate response: $response',
          framesProcessed: 0,
          inputBytes: 0,
          outputBytes: 0,
          durationSeconds: 0,
          destinationPath: destinationPath,
          frameTypeCounts: const {},
          compressionRatio: 0,
        );
      }
    } catch (e, stack) {
      return G723ConversionResult(
        success: false,
        errorMessage: 'Failed to run conversion isolate: $e\n$stack',
        framesProcessed: 0,
        inputBytes: 0,
        outputBytes: 0,
        durationSeconds: 0,
        destinationPath: destinationPath,
        frameTypeCounts: const {},
        compressionRatio: 0,
      );
    } finally {
      if (cancelCallback != null && cancellationToken != null) {
        cancellationToken._removeListener(cancelCallback);
      }
      replyPort.close();
      progressPort?.close();
      isolate?.kill(priority: Isolate.immediate);
    }
  }

  /// Worker isolate entrypoint.
  static void _isolateWorker(_ConverterConfig config) {
    try {
      final result = convertSync(
        mode: config.mode,
        sourcePath: config.sourcePath,
        destinationPath: config.destinationPath,
        onProgress: (progress, status) {
          config.progressPort?.send(_ProgressMessage(progress, status));
        },
      );
      config.replyPort.send(result);
    } catch (e, stack) {
      config.replyPort.send(_ErrorMessage('Conversion error: $e\n$stack'));
    }
  }

  /// Synchronous streaming file conversion.
  ///
  /// Can be invoked directly or inside a worker isolate.
  static G723ConversionResult convertSync({
    required G723ConversionMode mode,
    required String sourcePath,
    required String destinationPath,
    void Function(double progress, String status)? onProgress,
  }) {
    final sourceFile = File(sourcePath);
    if (!sourceFile.existsSync()) {
      return G723ConversionResult(
        success: false,
        errorMessage: 'Source file does not exist: $sourcePath',
        framesProcessed: 0,
        inputBytes: 0,
        outputBytes: 0,
        durationSeconds: 0,
        destinationPath: destinationPath,
        frameTypeCounts: const {},
        compressionRatio: 0,
      );
    }

    final destFile = File(destinationPath);
    destFile.parent.createSync(recursive: true);

    switch (mode) {
      case G723ConversionMode.pcmToG723_53k:
        return _encodePcmStreaming(
          sourceFile: sourceFile,
          destFile: destFile,
          bitrate: G723Bitrate.kbps53,
          onProgress: onProgress,
        );

      case G723ConversionMode.pcmToG723_63k:
        return _encodePcmStreaming(
          sourceFile: sourceFile,
          destFile: destFile,
          bitrate: G723Bitrate.kbps63,
          onProgress: onProgress,
        );

      case G723ConversionMode.g723ToPcm8k:
        return _decodeG723Streaming(
          sourceFile: sourceFile,
          destFile: destFile,
          targetSampleRate: 8000,
          onProgress: onProgress,
        );

      case G723ConversionMode.g723ToPcm16k:
        return _decodeG723Streaming(
          sourceFile: sourceFile,
          destFile: destFile,
          targetSampleRate: 16000,
          onProgress: onProgress,
        );

      case G723ConversionMode.roundtrip53k:
        return _roundtripStreaming(
          sourceFile: sourceFile,
          destFile: destFile,
          bitrate: G723Bitrate.kbps53,
          onProgress: onProgress,
        );

      case G723ConversionMode.roundtrip63k:
        return _roundtripStreaming(
          sourceFile: sourceFile,
          destFile: destFile,
          bitrate: G723Bitrate.kbps63,
          onProgress: onProgress,
        );
    }
  }

  static G723ConversionResult _encodePcmStreaming({
    required File sourceFile,
    required File destFile,
    required G723Bitrate bitrate,
    void Function(double progress, String status)? onProgress,
  }) {
    final inputTotalBytes = sourceFile.lengthSync();
    final source = sourceFile.openSync(mode: FileMode.read);
    final dest = destFile.openSync(mode: FileMode.write);

    G723Encoder? encoder;
    try {
      final wavHeader = WavHeader.readHeader(source);
      encoder = G723Encoder(
        bitrate: bitrate,
        sampleRate: wavHeader.sampleRate,
      );

      final samplesPerFrame = encoder.samplesPerFrame;
      final channels = wavHeader.numChannels;
      final bytesPerFrame = samplesPerFrame * channels * 2;
      final totalSamples = wavHeader.totalSamples;
      final totalFrames = (totalSamples + samplesPerFrame - 1) ~/ samplesPerFrame;

      const batchFrames = 64;
      final batchBytes = batchFrames * bytesPerFrame;
      final rawBatch = Uint8List(batchBytes);
      final framePcm = Int16List(samplesPerFrame);
      final outBuilder = BytesBuilder(copy: false);
      final counts = <String, int>{};

      int framesProcessed = 0;
      int totalOutputBytes = 0;
      final dataEndPos = wavHeader.dataOffset + wavHeader.dataBytes;

      while (source.positionSync() < dataEndPos) {
        final remainingInChunk = dataEndPos - source.positionSync();
        final toRead = math.min(batchBytes, remainingInChunk);
        final bytesRead = source.readIntoSync(rawBatch, 0, toRead);
        if (bytesRead <= 0) break;

        final framesInBatch = (bytesRead + bytesPerFrame - 1) ~/ bytesPerFrame;
        final rawByteData = ByteData.sublistView(rawBatch, 0, bytesRead);

        for (int f = 0; f < framesInBatch; f++) {
          final frameOffset = f * bytesPerFrame;
          final availableInFrame = math.min(bytesPerFrame, bytesRead - frameOffset);
          if (availableInFrame <= 0) break;

          final sampleCount = availableInFrame ~/ (channels * 2);

          if (channels == 1) {
            for (int s = 0; s < sampleCount; s++) {
              framePcm[s] = rawByteData.getInt16(frameOffset + s * 2, Endian.little);
            }
          } else {
            for (int s = 0; s < sampleCount; s++) {
              final left = rawByteData.getInt16(frameOffset + s * 4, Endian.little);
              final right = rawByteData.getInt16(frameOffset + s * 4 + 2, Endian.little);
              framePcm[s] = ((left + right) ~/ 2).clamp(-32768, 32767);
            }
          }

          // Zero-pad remainder of frame if last frame has fewer samples
          for (int s = sampleCount; s < samplesPerFrame; s++) {
            framePcm[s] = 0;
          }

          final encoded = encoder.encodeFrame(framePcm);
          outBuilder.add(encoded);

          final typeName = G723Codec.frameType(encoded).name;
          counts[typeName] = (counts[typeName] ?? 0) + 1;
          framesProcessed++;
        }

        final encodedChunk = outBuilder.takeBytes();
        dest.writeFromSync(encodedChunk);
        totalOutputBytes += encodedChunk.length;

        if (onProgress != null && (framesProcessed % 32 == 0 || framesProcessed >= totalFrames)) {
          final prog = totalFrames > 0 ? (framesProcessed / totalFrames).clamp(0.0, 1.0) : 0.0;
          onProgress(prog, 'Encoding frame $framesProcessed/$totalFrames...');
        }
      }

      final duration = framesProcessed * 0.030;
      final ratio = totalOutputBytes > 0 ? inputTotalBytes / totalOutputBytes : 0.0;

      return G723ConversionResult(
        success: true,
        framesProcessed: framesProcessed,
        inputBytes: inputTotalBytes,
        outputBytes: totalOutputBytes,
        durationSeconds: duration,
        destinationPath: destFile.path,
        frameTypeCounts: counts,
        compressionRatio: ratio,
      );
    } finally {
      encoder?.close();
      try {
        source.closeSync();
      } catch (_) {}
      try {
        dest.closeSync();
      } catch (_) {}
    }
  }

  static G723ConversionResult _decodeG723Streaming({
    required File sourceFile,
    required File destFile,
    required int targetSampleRate,
    void Function(double progress, String status)? onProgress,
  }) {
    final inputTotalBytes = sourceFile.lengthSync();
    final source = sourceFile.openSync(mode: FileMode.read);
    final dest = destFile.openSync(mode: FileMode.write);

    // Reserve 44 bytes for WAV header
    dest.writeFromSync(Uint8List(44));

    G723Decoder? decoder;
    try {
      decoder = G723Decoder(sampleRate: targetSampleRate);

      final counts = <String, int>{};
      int totalDecodedSamples = 0;
      int framesProcessed = 0;

      // 64 KB read buffer
      const readChunkSize = 65536;
      final readBuffer = Uint8List(readChunkSize);
      int bufferLen = 0;
      int bufferOffset = 0;

      // Output buffer for PCM bytes
      const pcmBufferSize = 65536;
      final pcmBytes = Uint8List(pcmBufferSize);
      final pcmByteData = ByteData.sublistView(pcmBytes);
      int pcmBytesWritten = 0;
      int totalOutputBytes = 44;

      while (true) {
        // Move unconsumed bytes to start of buffer
        final remainingInBuffer = bufferLen - bufferOffset;
        if (remainingInBuffer > 0 && bufferOffset > 0) {
          readBuffer.setRange(0, remainingInBuffer, readBuffer.sublist(bufferOffset, bufferLen));
        }
        bufferLen = remainingInBuffer;
        bufferOffset = 0;

        // Fill remaining buffer from file
        final bytesRead = source.readIntoSync(readBuffer, bufferLen, readChunkSize);
        bufferLen += bytesRead;

        if (bufferLen == 0) break;

        bool parsedAnyInLoop = false;

        while (bufferOffset < bufferLen) {
          final firstByte = readBuffer[bufferOffset];
          final rateBits = firstByte & 0x03;
          final frameSize = switch (rateBits) {
            0 => G723Codec.frameBytes63,
            1 => G723Codec.frameBytes53,
            2 => G723Codec.frameBytesSid,
            3 => G723Codec.frameBytesUntransmitted,
            _ => 0,
          };

          if (frameSize == 0) {
            // Skip invalid byte
            bufferOffset++;
            continue;
          }

          if (bufferOffset + frameSize > bufferLen) {
            // Need more data from disk to complete this frame
            break;
          }

          parsedAnyInLoop = true;
          final frame = readBuffer.sublist(bufferOffset, bufferOffset + frameSize);
          bufferOffset += frameSize;

          final pcm = decoder.decodeFrame(frame);
          totalDecodedSamples += pcm.length;

          final typeName = G723Codec.frameType(frame).name;
          counts[typeName] = (counts[typeName] ?? 0) + 1;
          framesProcessed++;

          // Write PCM samples to pcmBytes buffer
          for (int s = 0; s < pcm.length; s++) {
            if (pcmBytesWritten + 2 > pcmBufferSize) {
              dest.writeFromSync(pcmBytes, 0, pcmBytesWritten);
              totalOutputBytes += pcmBytesWritten;
              pcmBytesWritten = 0;
            }
            pcmByteData.setInt16(pcmBytesWritten, pcm[s], Endian.little);
            pcmBytesWritten += 2;
          }

          if (onProgress != null && framesProcessed % 32 == 0) {
            final filePos = source.positionSync() - (bufferLen - bufferOffset);
            final prog = inputTotalBytes > 0 ? (filePos / inputTotalBytes).clamp(0.0, 1.0) : 0.0;
            onProgress(prog, 'Decoding frame $framesProcessed...');
          }
        }

        if (!parsedAnyInLoop && bytesRead == 0) {
          // EOF reached and remaining buffer cannot form a valid frame
          break;
        }
      }

      // Flush remaining PCM bytes
      if (pcmBytesWritten > 0) {
        dest.writeFromSync(pcmBytes, 0, pcmBytesWritten);
        totalOutputBytes += pcmBytesWritten;
        pcmBytesWritten = 0;
      }

      // Write finalized WAV header at offset 0
      final wavHeader = WavHeader.createHeader(
        sampleRate: targetSampleRate,
        numChannels: 1,
        totalSamples: totalDecodedSamples,
      );
      dest.setPositionSync(0);
      dest.writeFromSync(wavHeader);

      if (onProgress != null) {
        onProgress(1.0, 'Decoding complete: $framesProcessed frames.');
      }

      final duration = framesProcessed * 0.030;
      final ratio = inputTotalBytes > 0 ? totalOutputBytes / inputTotalBytes : 0.0;

      return G723ConversionResult(
        success: true,
        framesProcessed: framesProcessed,
        inputBytes: inputTotalBytes,
        outputBytes: totalOutputBytes,
        durationSeconds: duration,
        destinationPath: destFile.path,
        frameTypeCounts: counts,
        compressionRatio: ratio,
      );
    } finally {
      decoder?.close();
      try {
        source.closeSync();
      } catch (_) {}
      try {
        dest.closeSync();
      } catch (_) {}
    }
  }

  static G723ConversionResult _roundtripStreaming({
    required File sourceFile,
    required File destFile,
    required G723Bitrate bitrate,
    void Function(double progress, String status)? onProgress,
  }) {
    final inputTotalBytes = sourceFile.lengthSync();
    final source = sourceFile.openSync(mode: FileMode.read);
    final dest = destFile.openSync(mode: FileMode.write);

    // Reserve 44 bytes for WAV header
    dest.writeFromSync(Uint8List(44));

    G723Encoder? encoder;
    G723Decoder? decoder;
    try {
      final wavHeader = WavHeader.readHeader(source);
      encoder = G723Encoder(
        bitrate: bitrate,
        sampleRate: wavHeader.sampleRate,
      );
      decoder = G723Decoder(sampleRate: wavHeader.sampleRate);

      final samplesPerFrame = encoder.samplesPerFrame;
      final channels = wavHeader.numChannels;
      final bytesPerFrame = samplesPerFrame * channels * 2;
      final totalSamples = wavHeader.totalSamples;
      final totalFrames = (totalSamples + samplesPerFrame - 1) ~/ samplesPerFrame;

      const batchFrames = 64;
      final batchBytes = batchFrames * bytesPerFrame;
      final rawBatch = Uint8List(batchBytes);
      final framePcm = Int16List(samplesPerFrame);

      const pcmBufferSize = 65536;
      final pcmBytes = Uint8List(pcmBufferSize);
      final pcmByteData = ByteData.sublistView(pcmBytes);
      int pcmBytesWritten = 0;
      int totalOutputBytes = 44;
      int totalDecodedSamples = 0;
      int totalG723Bytes = 0;

      final counts = <String, int>{};
      int framesProcessed = 0;
      final dataEndPos = wavHeader.dataOffset + wavHeader.dataBytes;

      while (source.positionSync() < dataEndPos) {
        final remainingInChunk = dataEndPos - source.positionSync();
        final toRead = math.min(batchBytes, remainingInChunk);
        final bytesRead = source.readIntoSync(rawBatch, 0, toRead);
        if (bytesRead <= 0) break;

        final framesInBatch = (bytesRead + bytesPerFrame - 1) ~/ bytesPerFrame;
        final rawByteData = ByteData.sublistView(rawBatch, 0, bytesRead);

        for (int f = 0; f < framesInBatch; f++) {
          final frameOffset = f * bytesPerFrame;
          final availableInFrame = math.min(bytesPerFrame, bytesRead - frameOffset);
          if (availableInFrame <= 0) break;

          final sampleCount = availableInFrame ~/ (channels * 2);

          if (channels == 1) {
            for (int s = 0; s < sampleCount; s++) {
              framePcm[s] = rawByteData.getInt16(frameOffset + s * 2, Endian.little);
            }
          } else {
            for (int s = 0; s < sampleCount; s++) {
              final left = rawByteData.getInt16(frameOffset + s * 4, Endian.little);
              final right = rawByteData.getInt16(frameOffset + s * 4 + 2, Endian.little);
              framePcm[s] = ((left + right) ~/ 2).clamp(-32768, 32767);
            }
          }

          for (int s = sampleCount; s < samplesPerFrame; s++) {
            framePcm[s] = 0;
          }

          final encoded = encoder.encodeFrame(framePcm);
          totalG723Bytes += encoded.length;

          final decoded = decoder.decodeFrame(encoded);
          totalDecodedSamples += decoded.length;

          final typeName = G723Codec.frameType(encoded).name;
          counts[typeName] = (counts[typeName] ?? 0) + 1;
          framesProcessed++;

          for (int s = 0; s < decoded.length; s++) {
            if (pcmBytesWritten + 2 > pcmBufferSize) {
              dest.writeFromSync(pcmBytes, 0, pcmBytesWritten);
              totalOutputBytes += pcmBytesWritten;
              pcmBytesWritten = 0;
            }
            pcmByteData.setInt16(pcmBytesWritten, decoded[s], Endian.little);
            pcmBytesWritten += 2;
          }
        }

        if (onProgress != null && (framesProcessed % 32 == 0 || framesProcessed >= totalFrames)) {
          final prog = totalFrames > 0 ? (framesProcessed / totalFrames).clamp(0.0, 1.0) : 0.0;
          onProgress(prog, 'Roundtrip frame $framesProcessed/$totalFrames...');
        }
      }

      if (pcmBytesWritten > 0) {
        dest.writeFromSync(pcmBytes, 0, pcmBytesWritten);
        totalOutputBytes += pcmBytesWritten;
        pcmBytesWritten = 0;
      }

      // Write finalized WAV header at offset 0
      final wavHeaderOut = WavHeader.createHeader(
        sampleRate: wavHeader.sampleRate,
        numChannels: 1,
        totalSamples: totalDecodedSamples,
      );
      dest.setPositionSync(0);
      dest.writeFromSync(wavHeaderOut);

      if (onProgress != null) {
        onProgress(1.0, 'Roundtrip complete: $framesProcessed frames.');
      }

      final duration = framesProcessed * 0.030;
      final ratio = totalG723Bytes > 0 ? inputTotalBytes / totalG723Bytes : 1.0;

      return G723ConversionResult(
        success: true,
        framesProcessed: framesProcessed,
        inputBytes: inputTotalBytes,
        outputBytes: totalOutputBytes,
        durationSeconds: duration,
        destinationPath: destFile.path,
        frameTypeCounts: counts,
        compressionRatio: ratio,
      );
    } finally {
      encoder?.close();
      decoder?.close();
      try {
        source.closeSync();
      } catch (_) {}
      try {
        dest.closeSync();
      } catch (_) {}
    }
  }
}
