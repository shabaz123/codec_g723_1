import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:codec_g723_1_example/wav_io.dart';
import 'package:codec_g723_1_example/converter_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  final tempDir = Directory.systemTemp.createTempSync('g723_test_');

  tearDownAll(() {
    if (tempDir.existsSync()) {
      tempDir.deleteSync(recursive: true);
    }
  });

  group('WAV IO tests', () {
    test('generate, write, and read 8 kHz WAV', () {
      final generated = WavAudio.generateTestSignal(sampleRate: 8000, durationSeconds: 1.0);
      expect(generated.sampleRate, equals(8000));
      expect(generated.samples.length, equals(8000));

      final bytes = WavAudio.writeWav(samples: generated.samples, sampleRate: 8000);
      expect(bytes.length, equals(44 + 8000 * 2));

      final parsed = WavAudio.readWav(bytes);
      expect(parsed.sampleRate, equals(8000));
      expect(parsed.numChannels, equals(1));
      expect(parsed.samples.length, equals(8000));
      for (int i = 0; i < 8000; i++) {
        expect(parsed.samples[i], equals(generated.samples[i]));
      }
    });

    test('generate, write, and read 16 kHz WAV', () {
      final generated = WavAudio.generateTestSignal(sampleRate: 16000, durationSeconds: 0.5);
      expect(generated.sampleRate, equals(16000));
      expect(generated.samples.length, equals(8000));

      final bytes = WavAudio.writeWav(samples: generated.samples, sampleRate: 16000);
      final parsed = WavAudio.readWav(bytes);
      expect(parsed.sampleRate, equals(16000));
      expect(parsed.samples.length, equals(8000));
    });
  });

  group('ConverterService end-to-end tests', () {
    late String wav8kPath;
    late String wav16kPath;

    setUpAll(() {
      // Create source 8k WAV
      final audio8k = WavAudio.generateTestSignal(sampleRate: 8000, durationSeconds: 1.0);
      final bytes8k = WavAudio.writeWav(samples: audio8k.samples, sampleRate: 8000);
      final f8k = File('${tempDir.path}/input_8k.wav');
      f8k.writeAsBytesSync(bytes8k);
      wav8kPath = f8k.path;

      // Create source 16k WAV
      final audio16k = WavAudio.generateTestSignal(sampleRate: 16000, durationSeconds: 1.0);
      final bytes16k = WavAudio.writeWav(samples: audio16k.samples, sampleRate: 16000);
      final f16k = File('${tempDir.path}/input_16k.wav');
      f16k.writeAsBytesSync(bytes16k);
      wav16kPath = f16k.path;
    });

    test('PCM (8k) -> G.723.1 5.3k', () async {
      final destPath = '${tempDir.path}/output_5.3k.g723';
      final result = await ConverterService.convert(
        mode: ConversionMode.pcmToG723_53k,
        sourcePath: wav8kPath,
        destinationPath: destPath,
      );

      expect(result.success, isTrue);
      expect(result.framesProcessed, greaterThan(0));
      expect(File(destPath).existsSync(), isTrue);
      expect(File(destPath).lengthSync(), equals(result.outputBytes));
      expect(result.outputBytes, equals(result.framesProcessed * 20));
      expect(result.frameTypeCounts['kbps53'], equals(result.framesProcessed));
    });

    test('PCM (16k) -> G.723.1 6.3k', () async {
      final destPath = '${tempDir.path}/output_6.3k.g723';
      final result = await ConverterService.convert(
        mode: ConversionMode.pcmToG723_63k,
        sourcePath: wav16kPath,
        destinationPath: destPath,
      );

      expect(result.success, isTrue);
      expect(result.framesProcessed, greaterThan(0));
      expect(File(destPath).existsSync(), isTrue);
      expect(result.outputBytes, equals(result.framesProcessed * 24));
      expect(result.frameTypeCounts['kbps63'], equals(result.framesProcessed));
    });

    test('G.723.1 -> PCM 8k WAV', () async {
      final g723Path = '${tempDir.path}/output_6.3k.g723';
      final destWavPath = '${tempDir.path}/decoded_8k.wav';

      final result = await ConverterService.convert(
        mode: ConversionMode.g723ToPcm8k,
        sourcePath: g723Path,
        destinationPath: destWavPath,
      );

      expect(result.success, isTrue);
      expect(File(destWavPath).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(destWavPath).readAsBytesSync());
      expect(wav.sampleRate, equals(8000));
      expect(wav.samples.length, equals(result.framesProcessed * 240));
    });

    test('G.723.1 -> PCM 16k WAV', () async {
      final g723Path = '${tempDir.path}/output_5.3k.g723';
      final destWavPath = '${tempDir.path}/decoded_16k.wav';

      final result = await ConverterService.convert(
        mode: ConversionMode.g723ToPcm16k,
        sourcePath: g723Path,
        destinationPath: destWavPath,
      );

      expect(result.success, isTrue);
      expect(File(destWavPath).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(destWavPath).readAsBytesSync());
      expect(wav.sampleRate, equals(16000));
      expect(wav.samples.length, equals(result.framesProcessed * 480));
    });

    test('Roundtrip 5.3k (WAV -> G.723.1 -> WAV)', () async {
      final destWavPath = '${tempDir.path}/roundtrip_53k.wav';

      final result = await ConverterService.convert(
        mode: ConversionMode.roundtrip53k,
        sourcePath: wav8kPath,
        destinationPath: destWavPath,
      );

      expect(result.success, isTrue);
      expect(File(destWavPath).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(destWavPath).readAsBytesSync());
      expect(wav.sampleRate, equals(8000));
      expect(wav.samples.length, equals(result.framesProcessed * 240));
    });

    test('Roundtrip 6.3k (WAV -> G.723.1 -> WAV)', () async {
      final destWavPath = '${tempDir.path}/roundtrip_63k.wav';

      final result = await ConverterService.convert(
        mode: ConversionMode.roundtrip63k,
        sourcePath: wav16kPath,
        destinationPath: destWavPath,
      );

      expect(result.success, isTrue);
      expect(File(destWavPath).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(destWavPath).readAsBytesSync());
      expect(wav.sampleRate, equals(16000));
      expect(wav.samples.length, equals(result.framesProcessed * 480));
    });

    test('WAV container G.723.1 encode and decode', () async {
      final g723WavPath = '${tempDir.path}/output_container_63k.wav';
      final pcmDecWavPath = '${tempDir.path}/decoded_from_container_8k.wav';

      final encResult = await ConverterService.convert(
        mode: ConversionMode.pcmToG723_63k,
        sourcePath: wav8kPath,
        destinationPath: g723WavPath,
        outputWavHeader: true,
      );

      expect(encResult.success, isTrue);
      expect(File(g723WavPath).existsSync(), isTrue);
      expect(encResult.outputBytes, equals(44 + encResult.framesProcessed * 24));

      final decResult = await ConverterService.convert(
        mode: ConversionMode.g723ToPcm8k,
        sourcePath: g723WavPath,
        destinationPath: pcmDecWavPath,
      );

      expect(decResult.success, isTrue);
      expect(decResult.framesProcessed, equals(encResult.framesProcessed));
      expect(File(pcmDecWavPath).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(pcmDecWavPath).readAsBytesSync());
      expect(wav.sampleRate, equals(8000));
      expect(wav.samples.length, equals(encResult.framesProcessed * 240));
    });
  });
}
