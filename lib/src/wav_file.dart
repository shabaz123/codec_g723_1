import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

/// Metadata parsed from a WAV file's RIFF/WAVE header.
class WavHeader {
  const WavHeader({
    required this.audioFormat,
    required this.numChannels,
    required this.sampleRate,
    required this.bitsPerSample,
    required this.dataOffset,
    required this.dataBytes,
  });

  final int audioFormat;
  final int numChannels;
  final int sampleRate;
  final int bitsPerSample;
  final int dataOffset;
  final int dataBytes;

  int get totalSamples => dataBytes ~/ (numChannels * (bitsPerSample ~/ 8));

  /// Reads and parses the RIFF header from an open [RandomAccessFile].
  ///
  /// Resets position to [dataOffset] upon successful return.
  static WavHeader readHeader(RandomAccessFile file) {
    final fileLength = file.lengthSync();
    if (fileLength < 44) {
      throw const FormatException('File is too small to be a valid WAV file.');
    }

    file.setPositionSync(0);
    final headerBuf = file.readSync(12);
    if (headerBuf.length < 12) {
      throw const FormatException('Unexpected end of file while reading RIFF header.');
    }

    final riffTag = String.fromCharCodes(headerBuf.sublist(0, 4));
    if (riffTag != 'RIFF') {
      throw FormatException('Invalid RIFF header: expected "RIFF", got "$riffTag".');
    }

    final waveTag = String.fromCharCodes(headerBuf.sublist(8, 12));
    if (waveTag != 'WAVE') {
      throw FormatException('Invalid WAVE header: expected "WAVE", got "$waveTag".');
    }

    int? audioFormat;
    int? numChannels;
    int? sampleRate;
    int? bitsPerSample;
    int? dataOffset;
    int? dataBytes;

    // Scan chunks until 'data' chunk is found
    while (file.positionSync() + 8 <= fileLength) {
      final chunkHeader = file.readSync(8);
      if (chunkHeader.length < 8) break;

      final chunkId = String.fromCharCodes(chunkHeader.sublist(0, 4));
      final chunkByteData = ByteData.sublistView(chunkHeader);
      final chunkSize = chunkByteData.getUint32(4, Endian.little);

      if (chunkId == 'fmt ') {
        if (chunkSize < 16) {
          throw const FormatException('Invalid fmt chunk size in WAV file.');
        }
        final fmtBytes = file.readSync(chunkSize);
        final fmtByteData = ByteData.sublistView(fmtBytes);
        audioFormat = fmtByteData.getUint16(0, Endian.little);
        numChannels = fmtByteData.getUint16(2, Endian.little);
        sampleRate = fmtByteData.getUint32(4, Endian.little);
        bitsPerSample = fmtByteData.getUint16(14, Endian.little);

        // Word alignment padding if chunk size is odd
        if (chunkSize % 2 != 0 && file.positionSync() < fileLength) {
          file.readSync(1);
        }
      } else if (chunkId == 'data') {
        dataOffset = file.positionSync();
        final remaining = fileLength - dataOffset;
        dataBytes = chunkSize <= remaining && chunkSize != 0 ? chunkSize : remaining;
        break;
      } else {
        // Skip unknown chunk
        final skip = chunkSize + (chunkSize % 2 != 0 ? 1 : 0);
        final currentPos = file.positionSync();
        final targetPos = currentPos + skip;
        if (targetPos <= fileLength) {
          file.setPositionSync(targetPos);
        } else {
          file.setPositionSync(fileLength);
          break;
        }
      }
    }

    if (audioFormat == null || numChannels == null || sampleRate == null || bitsPerSample == null) {
      throw const FormatException('Missing "fmt " chunk in WAV file.');
    }
    if (dataOffset == null || dataBytes == null) {
      throw const FormatException('Missing "data" chunk in WAV file.');
    }
    if (audioFormat != 1) {
      throw FormatException(
        'Unsupported WAV audio format $audioFormat. Only uncompressed PCM (format 1) is supported.',
      );
    }
    if (bitsPerSample != 16) {
      throw FormatException(
        'Unsupported bit depth ($bitsPerSample-bit). Only 16-bit PCM WAV is supported.',
      );
    }
    if (numChannels != 1 && numChannels != 2) {
      throw FormatException(
        'Unsupported number of channels ($numChannels). Only mono (1) and stereo (2) are supported.',
      );
    }
    if (sampleRate != 8000 && sampleRate != 16000) {
      throw FormatException(
        'Unsupported sample rate ($sampleRate Hz). G.723.1 requires 8000 Hz or 16000 Hz PCM.',
      );
    }

    file.setPositionSync(dataOffset);

    return WavHeader(
      audioFormat: audioFormat,
      numChannels: numChannels,
      sampleRate: sampleRate,
      bitsPerSample: bitsPerSample,
      dataOffset: dataOffset,
      dataBytes: dataBytes,
    );
  }

  /// Creates a canonical 44-byte WAV header for 16-bit PCM audio.
  static Uint8List createHeader({
    required int sampleRate,
    required int numChannels,
    required int totalSamples,
  }) {
    final subChunk2Size = totalSamples * 2 * numChannels;
    final chunkSize = 36 + subChunk2Size;
    final byteRate = sampleRate * numChannels * 2;
    final blockAlign = numChannels * 2;

    final bytes = Uint8List(44);
    final byteData = ByteData.sublistView(bytes);

    // RIFF header
    bytes.setRange(0, 4, 'RIFF'.codeUnits);
    byteData.setUint32(4, chunkSize > 0xFFFFFFFF ? 0xFFFFFFFF : chunkSize, Endian.little);
    bytes.setRange(8, 12, 'WAVE'.codeUnits);

    // fmt chunk
    bytes.setRange(12, 16, 'fmt '.codeUnits);
    byteData.setUint32(16, 16, Endian.little); // Subchunk1Size
    byteData.setUint16(20, 1, Endian.little); // AudioFormat: 1 = PCM
    byteData.setUint16(22, numChannels, Endian.little);
    byteData.setUint32(24, sampleRate, Endian.little);
    byteData.setUint32(28, byteRate, Endian.little);
    byteData.setUint16(32, blockAlign, Endian.little);
    byteData.setUint16(34, 16, Endian.little); // BitsPerSample

    // data chunk
    bytes.setRange(36, 40, 'data'.codeUnits);
    byteData.setUint32(40, subChunk2Size > 0xFFFFFFFF ? 0xFFFFFFFF : subChunk2Size, Endian.little);

    return bytes;
  }
}

/// Represents in-memory parsed or generated 16-bit PCM audio.
class WavAudio {
  const WavAudio({
    required this.sampleRate,
    required this.numChannels,
    required this.samples,
  });

  final int sampleRate;
  final int numChannels;
  final Int16List samples;

  double get durationSeconds => samples.length / (sampleRate * numChannels);

  /// Parse a 16-bit PCM WAV file byte buffer into mono PCM samples.
  /// If stereo, channels are mixed down to mono.
  static WavAudio readWav(Uint8List bytes) {
    if (bytes.length < 44) {
      throw const FormatException('File is too small to be a valid WAV file.');
    }

    final byteData = ByteData.sublistView(bytes);

    final riffTag = String.fromCharCodes(bytes.sublist(0, 4));
    if (riffTag != 'RIFF') {
      throw FormatException('Invalid RIFF header: expected "RIFF", got "$riffTag".');
    }

    final waveTag = String.fromCharCodes(bytes.sublist(8, 12));
    if (waveTag != 'WAVE') {
      throw FormatException('Invalid WAVE header: expected "WAVE", got "$waveTag".');
    }

    int offset = 12;
    int? audioFormat;
    int? channels;
    int? sampleRate;
    int? bitsPerSample;
    Uint8List? pcmData;

    while (offset + 8 <= bytes.length) {
      final chunkId = String.fromCharCodes(bytes.sublist(offset, offset + 4));
      final chunkSize = byteData.getUint32(offset + 4, Endian.little);
      offset += 8;

      if (chunkId == 'fmt ') {
        if (chunkSize < 16) {
          throw const FormatException('Invalid fmt chunk size.');
        }
        audioFormat = byteData.getUint16(offset, Endian.little);
        channels = byteData.getUint16(offset + 2, Endian.little);
        sampleRate = byteData.getUint32(offset + 4, Endian.little);
        bitsPerSample = byteData.getUint16(offset + 14, Endian.little);
      } else if (chunkId == 'data') {
        final available = bytes.length - offset;
        final dataLen = chunkSize <= available ? chunkSize : available;
        pcmData = bytes.sublist(offset, offset + dataLen);
      }

      offset += chunkSize;
      if (chunkSize % 2 != 0) {
        offset += 1;
      }
    }

    if (audioFormat == null || channels == null || sampleRate == null || bitsPerSample == null) {
      throw const FormatException('Missing fmt chunk in WAV file.');
    }

    if (audioFormat != 1) {
      throw FormatException(
        'Unsupported audio format $audioFormat. Only uncompressed PCM (format 1) is supported.',
      );
    }

    if (bitsPerSample != 16) {
      throw FormatException(
        'Unsupported bit depth ($bitsPerSample-bit). Only 16-bit PCM WAV is supported.',
      );
    }

    if (pcmData == null) {
      throw const FormatException('Missing data chunk in WAV file.');
    }

    final totalSamples = pcmData.length ~/ 2;
    final rawSamples = Int16List(totalSamples);
    final pcmByteData = ByteData.sublistView(pcmData);
    for (int i = 0; i < totalSamples; i++) {
      rawSamples[i] = pcmByteData.getInt16(i * 2, Endian.little);
    }

    if (channels == 1) {
      return WavAudio(
        sampleRate: sampleRate,
        numChannels: 1,
        samples: rawSamples,
      );
    } else if (channels == 2) {
      final monoLength = totalSamples ~/ 2;
      final monoSamples = Int16List(monoLength);
      for (int i = 0; i < monoLength; i++) {
        final left = rawSamples[i * 2];
        final right = rawSamples[i * 2 + 1];
        monoSamples[i] = ((left + right) ~/ 2).clamp(-32768, 32767);
      }
      return WavAudio(
        sampleRate: sampleRate,
        numChannels: 1,
        samples: monoSamples,
      );
    } else {
      throw FormatException('Unsupported number of channels ($channels).');
    }
  }

  /// Creates a canonical 16-bit mono WAV byte array.
  static Uint8List writeWav({
    required Int16List samples,
    required int sampleRate,
    int numChannels = 1,
  }) {
    final header = WavHeader.createHeader(
      sampleRate: sampleRate,
      numChannels: numChannels,
      totalSamples: samples.length ~/ numChannels,
    );

    final subChunk2Size = samples.length * 2;
    final bytes = Uint8List(44 + subChunk2Size);
    bytes.setRange(0, 44, header);

    final byteData = ByteData.sublistView(bytes);
    int byteOffset = 44;
    for (int i = 0; i < samples.length; i++) {
      byteData.setInt16(byteOffset, samples[i], Endian.little);
      byteOffset += 2;
    }

    return bytes;
  }

  /// Generate harmonic test speech-like waveform.
  static WavAudio generateTestSignal({
    required int sampleRate,
    double durationSeconds = 3.0,
  }) {
    final totalSamples = (sampleRate * durationSeconds).round();
    final samples = Int16List(totalSamples);

    for (int i = 0; i < totalSamples; i++) {
      final t = i / sampleRate;
      final pitch = 150.0 + 30.0 * math.sin(2.0 * math.pi * 1.5 * t);
      final envelope = 0.5 * (1.0 - math.cos(2.0 * math.pi * t / durationSeconds));

      final v1 = 7000.0 * math.sin(2.0 * math.pi * pitch * t);
      final v2 = 5000.0 * math.sin(2.0 * math.pi * pitch * 2.0 * t);
      final v3 = 3000.0 * math.sin(2.0 * math.pi * pitch * 3.0 * t);
      final v4 = 1500.0 * math.sin(2.0 * math.pi * pitch * 4.0 * t);

      final val = (v1 + v2 + v3 + v4) * envelope;
      samples[i] = val.round().clamp(-32768, 32767);
    }

    return WavAudio(
      sampleRate: sampleRate,
      numChannels: 1,
      samples: samples,
    );
  }
}
