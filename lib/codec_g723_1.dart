library;

export 'src/converter.dart';
export 'src/wav_file.dart';

import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'codec_g723_1_bindings_generated.dart' as native;

enum G723Bitrate {
  kbps53(5300),
  kbps63(6300);

  const G723Bitrate(this.bitsPerSecond);

  final int bitsPerSecond;
}

enum G723FrameType { kbps63, kbps53, sid, untransmitted, invalid }

class G723CodecException implements Exception {
  const G723CodecException(this.message, {this.nativeCode});

  final String message;
  final int? nativeCode;

  @override
  String toString() {
    if (nativeCode == null) {
      return 'G723CodecException: $message';
    }

    return 'G723CodecException: $message '
        '(native error $nativeCode)';
  }
}

class G723UnsupportedException extends G723CodecException {
  const G723UnsupportedException(super.message)
    : super(nativeCode: -2);
}

class G723CodecInfo {
  const G723CodecInfo({
    required this.abiVersion,
    required this.backendName,
    required this.supports53k,
    required this.supports63k,
  });

  final int abiVersion;
  final String backendName;
  final bool supports53k;
  final bool supports63k;
}

abstract final class G723Codec {
  static const int samplesPerFrame = 240;
  static const int sampleRate = 8000;

  static const int samplesPerFrame16k = 480;
  static const int sampleRate16k = 16000;

  static const int frameBytes53 = 20;
  static const int frameBytes63 = 24;
  static const int frameBytesSid = 4;
  static const int frameBytesUntransmitted = 1;

  static int get abiVersion => native.g723_abi_version();

  static String get backendName {
    final pointer = native.g723_backend_name();

    if (pointer == ffi.nullptr) {
      return 'unknown';
    }

    return pointer.cast<Utf8>().toDartString();
  }

  static bool supportsBitrate(G723Bitrate bitrate) {
    return native.g723_backend_supports_bitrate(bitrate.bitsPerSecond) != 0;
  }

  static G723CodecInfo get info {
    return G723CodecInfo(
      abiVersion: abiVersion,
      backendName: backendName,
      supports53k: supportsBitrate(G723Bitrate.kbps53),
      supports63k: supportsBitrate(G723Bitrate.kbps63),
    );
  }

  static G723FrameType frameType(Uint8List frame) {
    if (frame.isEmpty) {
      return G723FrameType.invalid;
    }

    final input = calloc<ffi.Uint8>(frame.length);

    try {
      input.asTypedList(frame.length).setAll(0, frame);

      final value = native.g723_frame_type(input, frame.length);

      return switch (value) {
        0 => G723FrameType.kbps63,
        1 => G723FrameType.kbps53,
        2 => G723FrameType.sid,
        3 => G723FrameType.untransmitted,
        _ => G723FrameType.invalid,
      };
    } finally {
      calloc.free(input);
    }
  }

  static int expectedFrameSize(G723FrameType type) {
    final nativeType = switch (type) {
      G723FrameType.kbps63 => 0,
      G723FrameType.kbps53 => 1,
      G723FrameType.sid => 2,
      G723FrameType.untransmitted => 3,
      G723FrameType.invalid => 255,
    };

    return native.g723_frame_size(nativeType);
  }
}

class G723Encoder {
  G723Encoder({
    required this.bitrate,
    int sampleRate = G723Codec.sampleRate,
  }) : _sampleRate = sampleRate {
    if (sampleRate != G723Codec.sampleRate &&
        sampleRate != G723Codec.sampleRate16k) {
      throw ArgumentError.value(
        sampleRate,
        'sampleRate',
        'Sample rate must be either ${G723Codec.sampleRate} or '
            '${G723Codec.sampleRate16k} Hz.',
      );
    }

    final out = calloc<ffi.Pointer<ffi.Void>>();

    try {
      final result = native.g723_encoder_create(bitrate.bitsPerSecond, out);

      _throwIfError(result, operation: 'Creating G.723.1 encoder');

      if (out.value == ffi.nullptr) {
        throw const G723CodecException(
          'Native encoder creation returned a null handle.',
        );
      }

      _handle = out.value;

      if (sampleRate != G723Codec.sampleRate) {
        final srResult = native.g723_encoder_set_sample_rate(
          _handle,
          sampleRate,
        );
        _throwIfError(srResult, operation: 'Setting G.723.1 encoder sample rate');
      }
    } finally {
      calloc.free(out);
    }
  }

  final G723Bitrate bitrate;
  int _sampleRate;

  int get sampleRate => _sampleRate;

  int get samplesPerFrame => _sampleRate == G723Codec.sampleRate16k
      ? G723Codec.samplesPerFrame16k
      : G723Codec.samplesPerFrame;

  ffi.Pointer<ffi.Void> _handle = ffi.nullptr;
  bool _closed = false;

  bool get isClosed => _closed;

  void setSampleRate(int rate) {
    _checkOpen();

    if (rate != G723Codec.sampleRate && rate != G723Codec.sampleRate16k) {
      throw ArgumentError.value(
        rate,
        'rate',
        'Sample rate must be either ${G723Codec.sampleRate} or '
            '${G723Codec.sampleRate16k} Hz.',
      );
    }

    final result = native.g723_encoder_set_sample_rate(_handle, rate);
    _throwIfError(result, operation: 'Setting G.723.1 encoder sample rate');
    _sampleRate = rate;
  }

  Uint8List encodeFrame(Int16List pcm) {
    _checkOpen();

    final expectedSamples = samplesPerFrame;

    if (pcm.length != expectedSamples) {
      throw ArgumentError.value(
        pcm.length,
        'pcm.length',
        'A G.723.1 frame must contain exactly '
            '$expectedSamples samples at $_sampleRate Hz.',
      );
    }

    final nativePcm = calloc<ffi.Int16>(expectedSamples);

    final output = calloc<ffi.Uint8>(G723Codec.frameBytes63);

    final outputSize = calloc<ffi.Uint32>();

    try {
      nativePcm.asTypedList(expectedSamples).setAll(0, pcm);

      final result = native.g723_encode_frame(
        _handle,
        nativePcm,
        expectedSamples,
        output,
        G723Codec.frameBytes63,
        outputSize,
      );

      _throwIfError(result, operation: 'Encoding G.723.1 frame');

      final size = outputSize.value;

      if (size > G723Codec.frameBytes63) {
        throw G723CodecException(
          'Native encoder returned invalid frame size $size.',
        );
      }

      return Uint8List.fromList(output.asTypedList(size));
    } finally {
      calloc.free(outputSize);
      calloc.free(output);
      calloc.free(nativePcm);
    }
  }

  void close() {
    if (_closed) {
      return;
    }

    if (_handle != ffi.nullptr) {
      native.g723_encoder_destroy(_handle);
      _handle = ffi.nullptr;
    }

    _closed = true;
  }

  void _checkOpen() {
    if (_closed || _handle == ffi.nullptr) {
      throw StateError('The G.723.1 encoder has already been closed.');
    }
  }
}

class G723Decoder {
  G723Decoder({int sampleRate = G723Codec.sampleRate})
      : _sampleRate = sampleRate {
    if (sampleRate != G723Codec.sampleRate &&
        sampleRate != G723Codec.sampleRate16k) {
      throw ArgumentError.value(
        sampleRate,
        'sampleRate',
        'Sample rate must be either ${G723Codec.sampleRate} or '
            '${G723Codec.sampleRate16k} Hz.',
      );
    }

    final out = calloc<ffi.Pointer<ffi.Void>>();

    try {
      final result = native.g723_decoder_create(out);

      _throwIfError(result, operation: 'Creating G.723.1 decoder');

      if (out.value == ffi.nullptr) {
        throw const G723CodecException(
          'Native decoder creation returned a null handle.',
        );
      }

      _handle = out.value;

      if (sampleRate != G723Codec.sampleRate) {
        final srResult = native.g723_decoder_set_sample_rate(
          _handle,
          sampleRate,
        );
        _throwIfError(srResult, operation: 'Setting G.723.1 decoder sample rate');
      }
    } finally {
      calloc.free(out);
    }
  }

  int _sampleRate;

  int get sampleRate => _sampleRate;

  int get samplesPerFrame => _sampleRate == G723Codec.sampleRate16k
      ? G723Codec.samplesPerFrame16k
      : G723Codec.samplesPerFrame;

  ffi.Pointer<ffi.Void> _handle = ffi.nullptr;
  bool _closed = false;

  bool get isClosed => _closed;

  void setSampleRate(int rate) {
    _checkOpen();

    if (rate != G723Codec.sampleRate && rate != G723Codec.sampleRate16k) {
      throw ArgumentError.value(
        rate,
        'rate',
        'Sample rate must be either ${G723Codec.sampleRate} or '
            '${G723Codec.sampleRate16k} Hz.',
      );
    }

    final result = native.g723_decoder_set_sample_rate(_handle, rate);
    _throwIfError(result, operation: 'Setting G.723.1 decoder sample rate');
    _sampleRate = rate;
  }

  Int16List decodeFrame(Uint8List frame) {
    _checkOpen();

    if (frame.isEmpty) {
      throw ArgumentError.value(
        frame.length,
        'frame.length',
        'Encoded frame must not be empty.',
      );
    }

    final type = G723Codec.frameType(frame);
    final expectedSize = G723Codec.expectedFrameSize(type);

    if (type == G723FrameType.invalid || expectedSize == 0) {
      throw const G723CodecException('Invalid G.723.1 frame type.');
    }

    if (frame.length != expectedSize) {
      throw G723CodecException(
        'Invalid G.723.1 frame size: '
        'got ${frame.length}, expected $expectedSize.',
      );
    }

    final expectedSamples = samplesPerFrame;
    final input = calloc<ffi.Uint8>(frame.length);
    final pcm = calloc<ffi.Int16>(expectedSamples);
    final pcmSamples = calloc<ffi.Uint32>();

    try {
      input.asTypedList(frame.length).setAll(0, frame);

      final result = native.g723_decode_frame(
        _handle,
        input,
        frame.length,
        pcm,
        expectedSamples,
        pcmSamples,
      );

      _throwIfError(result, operation: 'Decoding G.723.1 frame');

      final count = pcmSamples.value;

      if (count > expectedSamples) {
        throw G723CodecException(
          'Native decoder returned invalid PCM '
          'sample count $count.',
        );
      }

      return Int16List.fromList(pcm.asTypedList(count));
    } finally {
      calloc.free(pcmSamples);
      calloc.free(pcm);
      calloc.free(input);
    }
  }

  void close() {
    if (_closed) {
      return;
    }

    if (_handle != ffi.nullptr) {
      native.g723_decoder_destroy(_handle);
      _handle = ffi.nullptr;
    }

    _closed = true;
  }

  void _checkOpen() {
    if (_closed || _handle == ffi.nullptr) {
      throw StateError('The G.723.1 decoder has already been closed.');
    }
  }
}

void _throwIfError(int result, {required String operation}) {
  switch (result) {
    case 0:
      return;

    case -1:
      throw G723CodecException(
        '$operation failed: invalid argument.',
        nativeCode: result,
      );

    case -2:
      throw G723UnsupportedException(
        '$operation is not supported by the '
        'current codec backend.',
      );

    case -3:
      throw G723CodecException(
        '$operation failed: bad G.723.1 frame.',
        nativeCode: result,
      );

    case -4:
      throw G723CodecException(
        '$operation failed: native memory allocation failed.',
        nativeCode: result,
      );

    case -5:
      throw G723CodecException(
        '$operation failed: internal native error.',
        nativeCode: result,
      );

    default:
      throw G723CodecException(
        '$operation failed with an unknown native error.',
        nativeCode: result,
      );
  }
}
