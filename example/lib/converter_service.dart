import 'package:codec_g723_1/codec_g723_1.dart';

export 'package:codec_g723_1/codec_g723_1.dart'
    show G723ConversionMode, G723ConversionResult, G723CancellationToken;

typedef ConversionMode = G723ConversionMode;
typedef ConversionResult = G723ConversionResult;

/// High-level service that delegates conversion to [G723FileConverter] in a background isolate.
abstract final class ConverterService {
  /// Converts an audio file in a background isolate with chunked streaming I/O.
  static Future<ConversionResult> convert({
    required ConversionMode mode,
    required String sourcePath,
    required String destinationPath,
    void Function(double progress, String status)? onProgress,
    G723CancellationToken? cancellationToken,
  }) {
    return G723FileConverter.convert(
      mode: mode,
      sourcePath: sourcePath,
      destinationPath: destinationPath,
      onProgress: onProgress,
      cancellationToken: cancellationToken,
    );
  }
}
