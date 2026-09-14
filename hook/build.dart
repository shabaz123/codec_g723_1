import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:logging/logging.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

const _sources = [
  'src/codec_g723_1.c',
  'src/backends/c/g723_backend_c.c',
  'src/backends/c/g723_basicop.c',
  'src/backends/c/g723_encoder.c',
  'src/backends/c/g723_header.c',
  'src/backends/c/g723_linepack.c',
  'src/backends/c/g723_qdec.c',
  'src/backends/c/g723_resample.c',
  'src/backends/c/g723_spec_exc.c',
  'src/backends/c/g723_spec_lsp.c',
  'src/backends/c/g723_tables.c',
];

const _includes = [
  'src',
  'src/backends/c',
];

void main(List<String> args) async {
  await build(args, (input, output) async {
    final packageName = input.packageName;
    final targetOS = input.config.code.targetOS;

    final cbuilder = CBuilder.library(
      name: packageName,
      assetName: '${packageName}_bindings_generated.dart',
      sources: _sources,
      includes: _includes,
      libraries: targetOS != OS.windows ? ['m'] : const [],
    );

    try {
      await cbuilder.run(
        input: input,
        output: output,
        logger: Logger('')
          ..level = Level.ALL
          // ignore: avoid_print
          ..onRecord.listen((record) => print(record.message)),
      );
    } catch (e) {
      // If targeting Windows and MSVC (cl.exe) is not installed,
      // fall back to MinGW / GCC / Clang on the host system.
      if (targetOS == OS.windows) {
        await _buildWindowsWithGcc(input, output);
      } else {
        rethrow;
      }
    }
  });
}

Future<void> _buildWindowsWithGcc(
  BuildInput input,
  BuildOutputBuilder output,
) async {
  final outDir = input.outputDirectory;
  final packageRoot = input.packageRoot;
  await Directory.fromUri(outDir).create(recursive: true);

  final libName = '${input.packageName}.dll';
  final libUri = outDir.resolve(libName);

  final sourceFiles = _sources
      .map((s) => packageRoot.resolve(s).toFilePath())
      .toList();

  final includeDirs = _includes
      .map((inc) => packageRoot.resolve(inc).toFilePath())
      .toList();

  // Search for GCC
  String gccPath = 'gcc';
  final candidateGcc = r'C:\DEV\vhd_mounts\msys2\msys64\mingw64\bin\gcc.exe';
  if (File(candidateGcc).existsSync()) {
    gccPath = candidateGcc;
  }

  final args = [
    '-O3',
    '-shared',
    '-static-libgcc',
    '-o',
    libUri.toFilePath(),
    ...sourceFiles,
    for (final inc in includeDirs) '-I$inc',
    '-lm',
  ];

  final env = Map<String, String>.from(Platform.environment);
  final mingwBin = r'C:\DEV\vhd_mounts\msys2\msys64\mingw64\bin';
  if (Directory(mingwBin).existsSync()) {
    final currentPath = env['PATH'] ?? '';
    env['PATH'] = '$mingwBin;$currentPath';
  }

  final proc = await Process.run(gccPath, args, environment: env);
  if (proc.exitCode != 0) {
    throw ProcessException(
      gccPath,
      args,
      'MinGW GCC build failed (exit ${proc.exitCode}):\n${proc.stdout}\n${proc.stderr}',
      proc.exitCode,
    );
  }

  output.assets.code.add(
    CodeAsset(
      package: input.packageName,
      name: '${input.packageName}_bindings_generated.dart',
      file: libUri,
      linkMode: DynamicLoadingBundled(),
    ),
  );
}
