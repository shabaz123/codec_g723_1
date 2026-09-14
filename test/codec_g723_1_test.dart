import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:codec_g723_1/codec_g723_1.dart';

void main() {
  group('G723Codec metadata and properties', () {
    test('abiVersion matches expected', () {
      expect(G723Codec.abiVersion, equals(1));
    });

    test('backendName reports pure C implementation', () {
      expect(G723Codec.backendName, equals('c'));
    });

    test('supports 6.3 kbps and 5.3 kbps bitrates', () {
      expect(G723Codec.supportsBitrate(G723Bitrate.kbps63), isTrue);
      expect(G723Codec.supportsBitrate(G723Bitrate.kbps53), isTrue);
    });

    test('codec info returns correct metadata', () {
      final info = G723Codec.info;
      expect(info.abiVersion, equals(1));
      expect(info.backendName, equals('c'));
      expect(info.supports63k, isTrue);
      expect(info.supports53k, isTrue);
    });

    test('expected frame sizes match specification', () {
      expect(
        G723Codec.expectedFrameSize(G723FrameType.kbps63),
        equals(G723Codec.frameBytes63),
      );
      expect(
        G723Codec.expectedFrameSize(G723FrameType.kbps53),
        equals(G723Codec.frameBytes53),
      );
      expect(
        G723Codec.expectedFrameSize(G723FrameType.sid),
        equals(G723Codec.frameBytesSid),
      );
      expect(
        G723Codec.expectedFrameSize(G723FrameType.untransmitted),
        equals(G723Codec.frameBytesUntransmitted),
      );
    });

    test('frameType correctly identifies frames', () {
      final frame63 = Uint8List(24);
      frame63[0] = 0x00; // Rate6300 flag
      expect(G723Codec.frameType(frame63), equals(G723FrameType.kbps63));

      final frame53 = Uint8List(20);
      frame53[0] = 0x01; // Rate5300 flag
      expect(G723Codec.frameType(frame53), equals(G723FrameType.kbps53));

      final frameSid = Uint8List(4);
      frameSid[0] = 0x02; // Sid flag
      expect(G723Codec.frameType(frameSid), equals(G723FrameType.sid));

      final frameUntransmitted = Uint8List(1);
      frameUntransmitted[0] = 0x03; // Untransmitted flag
      expect(
        G723Codec.frameType(frameUntransmitted),
        equals(G723FrameType.untransmitted),
      );

      expect(G723Codec.frameType(Uint8List(0)), equals(G723FrameType.invalid));
    });
  });

  group('8 kHz encoding and decoding', () {
    test('6.3 kbps encode and decode roundtrip at 8 kHz', () {
      final encoder = G723Encoder(bitrate: G723Bitrate.kbps63);
      final decoder = G723Decoder();

      expect(encoder.sampleRate, equals(8000));
      expect(decoder.sampleRate, equals(8000));
      expect(encoder.samplesPerFrame, equals(240));
      expect(decoder.samplesPerFrame, equals(240));

      const numFrames = 6;
      final allDecoded = <int>[];
      final allInput = <int>[];

      for (int f = 0; f < numFrames; f++) {
        final framePcm = Int16List(240);
        for (int i = 0; i < 240; i++) {
          final t = (f * 240 + i) / 8000.0;
          final val = 8000.0 * math.sin(2.0 * math.pi * 300.0 * t) +
              4000.0 * math.sin(2.0 * math.pi * 600.0 * t);
          framePcm[i] = val.round().clamp(-32768, 32767);
          allInput.add(framePcm[i]);
        }

        final encoded = encoder.encodeFrame(framePcm);
        expect(encoded.length, equals(G723Codec.frameBytes63));
        expect(G723Codec.frameType(encoded), equals(G723FrameType.kbps63));

        final decoded = decoder.decodeFrame(encoded);
        expect(decoded.length, equals(G723Codec.samplesPerFrame));
        allDecoded.addAll(decoded);
      }

      double sumEnergy = 0;
      for (final s in allDecoded) {
        sumEnergy += s * s;
      }
      expect(sumEnergy, greaterThan(0));

      // G.723.1 algorithmic lookahead is 60 samples (7.5 ms)
      const delay = 60;
      double sumProd = 0;
      double inEnergy = 0;
      double decEnergy = 0;
      for (int i = 60; i < allInput.length - delay; i++) {
        final xi = allInput[i].toDouble();
        final yi = allDecoded[i + delay].toDouble();
        sumProd += xi * yi;
        inEnergy += xi * xi;
        decEnergy += yi * yi;
      }
      final corr = sumProd / math.sqrt(inEnergy * decEnergy);
      expect(corr, greaterThan(0.65));

      encoder.close();
      decoder.close();
      expect(encoder.isClosed, isTrue);
      expect(decoder.isClosed, isTrue);
    });

    test('5.3 kbps encode and decode roundtrip at 8 kHz', () {
      final encoder = G723Encoder(bitrate: G723Bitrate.kbps53);
      final decoder = G723Decoder();

      expect(encoder.sampleRate, equals(8000));
      expect(decoder.sampleRate, equals(8000));

      const numFrames = 6;
      final allDecoded = <int>[];
      final allInput = <int>[];

      for (int f = 0; f < numFrames; f++) {
        final framePcm = Int16List(240);
        for (int i = 0; i < 240; i++) {
          final t = (f * 240 + i) / 8000.0;
          final val = 8000.0 * math.sin(2.0 * math.pi * 300.0 * t) +
              4000.0 * math.sin(2.0 * math.pi * 600.0 * t);
          framePcm[i] = val.round().clamp(-32768, 32767);
          allInput.add(framePcm[i]);
        }

        final encoded = encoder.encodeFrame(framePcm);
        expect(encoded.length, equals(G723Codec.frameBytes53));
        expect(G723Codec.frameType(encoded), equals(G723FrameType.kbps53));

        final decoded = decoder.decodeFrame(encoded);
        expect(decoded.length, equals(G723Codec.samplesPerFrame));
        allDecoded.addAll(decoded);
      }

      double sumEnergy = 0;
      for (final s in allDecoded) {
        sumEnergy += s * s;
      }
      expect(sumEnergy, greaterThan(0));

      const delay = 60;
      double sumProd = 0;
      double inEnergy = 0;
      double decEnergy = 0;
      for (int i = 60; i < allInput.length - delay; i++) {
        final xi = allInput[i].toDouble();
        final yi = allDecoded[i + delay].toDouble();
        sumProd += xi * yi;
        inEnergy += xi * xi;
        decEnergy += yi * yi;
      }
      final corr = sumProd / math.sqrt(inEnergy * decEnergy);
      expect(corr, greaterThan(0.60));

      encoder.close();
      decoder.close();
    });
  });

  group('16 kHz encoding and decoding', () {
    test('6.3 kbps encode and decode roundtrip at 16 kHz', () {
      final encoder = G723Encoder(
        bitrate: G723Bitrate.kbps63,
        sampleRate: G723Codec.sampleRate16k,
      );
      final decoder = G723Decoder(sampleRate: G723Codec.sampleRate16k);

      expect(encoder.sampleRate, equals(16000));
      expect(decoder.sampleRate, equals(16000));
      expect(encoder.samplesPerFrame, equals(480));
      expect(decoder.samplesPerFrame, equals(480));

      const numFrames = 6;
      final allDecoded = <int>[];
      final allInput = <int>[];

      for (int f = 0; f < numFrames; f++) {
        final framePcm = Int16List(480);
        for (int i = 0; i < 480; i++) {
          final t = (f * 480 + i) / 16000.0;
          final val = 8000.0 * math.sin(2.0 * math.pi * 300.0 * t) +
              4000.0 * math.sin(2.0 * math.pi * 600.0 * t);
          framePcm[i] = val.round().clamp(-32768, 32767);
          allInput.add(framePcm[i]);
        }

        final encoded = encoder.encodeFrame(framePcm);
        expect(encoded.length, equals(G723Codec.frameBytes63));
        expect(G723Codec.frameType(encoded), equals(G723FrameType.kbps63));

        final decoded = decoder.decodeFrame(encoded);
        expect(decoded.length, equals(G723Codec.samplesPerFrame16k));
        allDecoded.addAll(decoded);
      }

      double sumEnergy = 0;
      for (final s in allDecoded) {
        sumEnergy += s * s;
      }
      expect(sumEnergy, greaterThan(0));

      // Find max correlation over reasonable lag range around 60 samples @ 8kHz (120 samples @ 16kHz) + resampler delay
      double maxCorr = -1.0;
      for (int lag = 100; lag <= 180; lag++) {
        double sumProd = 0;
        double inEnergy = 0;
        double decEnergy = 0;
        for (int i = 150; i < allInput.length - lag; i++) {
          final xi = allInput[i].toDouble();
          final yi = allDecoded[i + lag].toDouble();
          sumProd += xi * yi;
          inEnergy += xi * xi;
          decEnergy += yi * yi;
        }
        if (inEnergy > 0 && decEnergy > 0) {
          final corr = sumProd / math.sqrt(inEnergy * decEnergy);
          if (corr > maxCorr) {
            maxCorr = corr;
          }
        }
      }
      expect(maxCorr, greaterThan(0.60));

      encoder.close();
      decoder.close();
    });

    test('5.3 kbps encode and decode roundtrip at 16 kHz', () {
      final encoder = G723Encoder(
        bitrate: G723Bitrate.kbps53,
        sampleRate: 16000,
      );
      final decoder = G723Decoder(sampleRate: 16000);

      expect(encoder.sampleRate, equals(16000));
      expect(decoder.sampleRate, equals(16000));

      final testSignal16k = Int16List(480);
      for (int i = 0; i < 480; i++) {
        final val = 8000.0 * math.sin(2.0 * math.pi * 300.0 * i / 16000.0);
        testSignal16k[i] = val.round().clamp(-32768, 32767);
      }

      final encoded = encoder.encodeFrame(testSignal16k);
      expect(encoded.length, equals(G723Codec.frameBytes53));
      expect(G723Codec.frameType(encoded), equals(G723FrameType.kbps53));

      final decoded = decoder.decodeFrame(encoded);
      expect(decoded.length, equals(G723Codec.samplesPerFrame16k));

      double sumEnergy = 0;
      for (int i = 0; i < decoded.length; i++) {
        sumEnergy += decoded[i] * decoded[i];
      }
      expect(sumEnergy, greaterThan(0));

      encoder.close();
      decoder.close();
    });

    test('dynamic sampleRate switching on encoder and decoder', () {
      final encoder = G723Encoder(bitrate: G723Bitrate.kbps63);
      final decoder = G723Decoder();

      expect(encoder.sampleRate, equals(8000));
      expect(decoder.sampleRate, equals(8000));

      encoder.setSampleRate(16000);
      decoder.setSampleRate(16000);

      expect(encoder.sampleRate, equals(16000));
      expect(decoder.sampleRate, equals(16000));
      expect(encoder.samplesPerFrame, equals(480));
      expect(decoder.samplesPerFrame, equals(480));

      final testSignal16k = Int16List(480);
      for (int i = 0; i < 480; i++) {
        final val = 8000.0 * math.sin(2.0 * math.pi * 300.0 * i / 16000.0);
        testSignal16k[i] = val.round().clamp(-32768, 32767);
      }

      final encoded = encoder.encodeFrame(testSignal16k);
      expect(encoded.length, equals(24));
      final decoded = decoder.decodeFrame(encoded);
      expect(decoded.length, equals(480));

      encoder.close();
      decoder.close();
    });
  });

  group('Validation and error handling', () {
    test('invalid sample rates are rejected', () {
      expect(
        () => G723Encoder(bitrate: G723Bitrate.kbps63, sampleRate: 44100),
        throwsArgumentError,
      );
      expect(() => G723Decoder(sampleRate: 48000), throwsArgumentError);
    });

    test('invalid PCM sample count throws ArgumentError', () {
      final encoder8k = G723Encoder(bitrate: G723Bitrate.kbps63);
      expect(
        () => encoder8k.encodeFrame(Int16List(100)),
        throwsArgumentError,
      );
      encoder8k.close();

      final encoder16k = G723Encoder(
        bitrate: G723Bitrate.kbps63,
        sampleRate: 16000,
      );
      expect(
        () => encoder16k.encodeFrame(Int16List(240)),
        throwsArgumentError,
      );
      encoder16k.close();
    });

    test('decoding empty frame throws ArgumentError', () {
      final decoder = G723Decoder();
      expect(() => decoder.decodeFrame(Uint8List(0)), throwsArgumentError);
      decoder.close();
    });

    test('operating on closed encoder/decoder throws StateError', () {
      final encoder = G723Encoder(bitrate: G723Bitrate.kbps63);
      encoder.close();
      expect(
        () => encoder.encodeFrame(Int16List(240)),
        throwsStateError,
      );

      final decoder = G723Decoder();
      decoder.close();
      expect(
        () => decoder.decodeFrame(Uint8List(24)),
        throwsStateError,
      );
    });
  });

  group('WAV streaming header and audio utilities', () {
    test('WavAudio and WavHeader roundtrip 8 kHz', () {
      final audio = WavAudio.generateTestSignal(sampleRate: 8000, durationSeconds: 0.5);
      final wavBytes = WavAudio.writeWav(samples: audio.samples, sampleRate: 8000);
      final parsed = WavAudio.readWav(wavBytes);

      expect(parsed.sampleRate, equals(8000));
      expect(parsed.numChannels, equals(1));
      expect(parsed.samples.length, equals(audio.samples.length));
      expect(parsed.durationSeconds, closeTo(0.5, 0.01));
    });

    test('WavAudio and WavHeader roundtrip 16 kHz', () {
      final audio = WavAudio.generateTestSignal(sampleRate: 16000, durationSeconds: 0.5);
      final wavBytes = WavAudio.writeWav(samples: audio.samples, sampleRate: 16000);
      final parsed = WavAudio.readWav(wavBytes);

      expect(parsed.sampleRate, equals(16000));
      expect(parsed.numChannels, equals(1));
      expect(parsed.samples.length, equals(audio.samples.length));
    });

    test('stereo WAV downmixes to mono', () {
      final left = Int16List.fromList([1000, 2000, 3000]);
      final right = Int16List.fromList([3000, 4000, 5000]);
      final interleaved = Int16List(6);
      for (int i = 0; i < 3; i++) {
        interleaved[i * 2] = left[i];
        interleaved[i * 2 + 1] = right[i];
      }
      final wavBytes = WavAudio.writeWav(
        samples: interleaved,
        sampleRate: 8000,
        numChannels: 2,
      );
      final parsed = WavAudio.readWav(wavBytes);
      expect(parsed.numChannels, equals(1));
      expect(parsed.samples.length, equals(3));
      expect(parsed.samples[0], equals(2000));
      expect(parsed.samples[1], equals(3000));
      expect(parsed.samples[2], equals(4000));
    });
  });

  group('G723FileConverter background isolate conversions', () {
    late Directory tempDir;
    late String wav8kPath;
    late String wav16kPath;

    setUpAll(() {
      tempDir = Directory.systemTemp.createTempSync('g723_pkg_test_');

      final audio8k = WavAudio.generateTestSignal(sampleRate: 8000, durationSeconds: 1.0);
      final bytes8k = WavAudio.writeWav(samples: audio8k.samples, sampleRate: 8000);
      final f8k = File('${tempDir.path}/input_8k.wav');
      f8k.writeAsBytesSync(bytes8k);
      wav8kPath = f8k.path;

      final audio16k = WavAudio.generateTestSignal(sampleRate: 16000, durationSeconds: 1.0);
      final bytes16k = WavAudio.writeWav(samples: audio16k.samples, sampleRate: 16000);
      final f16k = File('${tempDir.path}/input_16k.wav');
      f16k.writeAsBytesSync(bytes16k);
      wav16kPath = f16k.path;
    });

    tearDownAll(() async {
      await Future<void>.delayed(const Duration(milliseconds: 150));
      if (tempDir.existsSync()) {
        try {
          tempDir.deleteSync(recursive: true);
        } catch (_) {}
      }
    });

    test('PCM 8k to G.723.1 5.3k with progress tracking', () async {
      final destPath = '${tempDir.path}/pkg_out_53k.g723';
      final progressUpdates = <double>[];

      final result = await G723FileConverter.convert(
        mode: G723ConversionMode.pcmToG723_53k,
        sourcePath: wav8kPath,
        destinationPath: destPath,
        onProgress: (prog, status) {
          progressUpdates.add(prog);
        },
      );

      expect(result.success, isTrue);
      expect(result.framesProcessed, greaterThan(0));
      expect(File(destPath).existsSync(), isTrue);
      expect(result.outputBytes, equals(result.framesProcessed * 20));
      expect(result.frameTypeCounts['kbps53'], equals(result.framesProcessed));
      expect(progressUpdates, isNotEmpty);
    });

    test('PCM 16k to G.723.1 6.3k with streaming chunked I/O', () async {
      final destPath = '${tempDir.path}/pkg_out_63k.g723';

      final result = await G723FileConverter.convert(
        mode: G723ConversionMode.pcmToG723_63k,
        sourcePath: wav16kPath,
        destinationPath: destPath,
      );

      expect(result.success, isTrue);
      expect(result.framesProcessed, greaterThan(0));
      expect(File(destPath).existsSync(), isTrue);
      expect(result.outputBytes, equals(result.framesProcessed * 24));
      expect(result.frameTypeCounts['kbps63'], equals(result.framesProcessed));
    });

    test('G.723.1 to PCM 8k WAV decoding in isolate', () async {
      final g723Path = '${tempDir.path}/pkg_out_63k.g723';
      final destWav = '${tempDir.path}/pkg_dec_8k.wav';

      final result = await G723FileConverter.convert(
        mode: G723ConversionMode.g723ToPcm8k,
        sourcePath: g723Path,
        destinationPath: destWav,
      );

      expect(result.success, isTrue);
      expect(File(destWav).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(destWav).readAsBytesSync());
      expect(wav.sampleRate, equals(8000));
      expect(wav.samples.length, equals(result.framesProcessed * 240));
    });

    test('G.723.1 to PCM 16k WAV decoding in isolate', () async {
      final g723Path = '${tempDir.path}/pkg_out_53k.g723';
      final destWav = '${tempDir.path}/pkg_dec_16k.wav';

      final result = await G723FileConverter.convert(
        mode: G723ConversionMode.g723ToPcm16k,
        sourcePath: g723Path,
        destinationPath: destWav,
      );

      expect(result.success, isTrue);
      expect(File(destWav).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(destWav).readAsBytesSync());
      expect(wav.sampleRate, equals(16000));
      expect(wav.samples.length, equals(result.framesProcessed * 480));
    });

    test('Roundtrip 5.3k streaming conversion in isolate', () async {
      final destWav = '${tempDir.path}/pkg_roundtrip_53k.wav';

      final result = await G723FileConverter.convert(
        mode: G723ConversionMode.roundtrip53k,
        sourcePath: wav8kPath,
        destinationPath: destWav,
      );

      expect(result.success, isTrue);
      expect(File(destWav).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(destWav).readAsBytesSync());
      expect(wav.sampleRate, equals(8000));
      expect(wav.samples.length, equals(result.framesProcessed * 240));
    });

    test('Roundtrip 6.3k streaming conversion in isolate', () async {
      final destWav = '${tempDir.path}/pkg_roundtrip_63k.wav';

      final result = await G723FileConverter.convert(
        mode: G723ConversionMode.roundtrip63k,
        sourcePath: wav16kPath,
        destinationPath: destWav,
      );

      expect(result.success, isTrue);
      expect(File(destWav).existsSync(), isTrue);

      final wav = WavAudio.readWav(File(destWav).readAsBytesSync());
      expect(wav.sampleRate, equals(16000));
      expect(wav.samples.length, equals(result.framesProcessed * 480));
    });

    test('Handles missing source file gracefully', () async {
      final result = await G723FileConverter.convert(
        mode: G723ConversionMode.pcmToG723_63k,
        sourcePath: '${tempDir.path}/nonexistent.wav',
        destinationPath: '${tempDir.path}/wont_exist.g723',
      );

      expect(result.success, isFalse);
      expect(result.errorMessage, contains('does not exist'));
    });

    test('Cancellation token terminates conversion immediately', () async {
      // Create a longer 5-second WAV file for cancellation testing
      final audio = WavAudio.generateTestSignal(sampleRate: 8000, durationSeconds: 5.0);
      final bytes = WavAudio.writeWav(samples: audio.samples, sampleRate: 8000);
      final longWav = File('${tempDir.path}/cancel_test.wav');
      longWav.writeAsBytesSync(bytes);

      final token = G723CancellationToken();
      final destPath = '${tempDir.path}/cancelled.g723';

      final future = G723FileConverter.convert(
        mode: G723ConversionMode.pcmToG723_63k,
        sourcePath: longWav.path,
        destinationPath: destPath,
        cancellationToken: token,
        onProgress: (prog, status) {
          // Cancel on first progress update
          token.cancel();
        },
      );

      final result = await future;
      expect(result.success, isFalse);
      expect(result.errorMessage, contains('cancelled'));
    });
  });
}

