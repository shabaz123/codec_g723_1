import 'dart:io';

import 'package:codec_g723_1/codec_g723_1.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';

import 'converter_service.dart';

void main() {
  runApp(const G723ConverterApp());
}

class G723ConverterApp extends StatelessWidget {
  const G723ConverterApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'G.723.1 Audio Converter',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF1565C0),
          brightness: Brightness.light,
        ),
        useMaterial3: true,
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF1976D2),
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      themeMode: ThemeMode.system,
      home: const G723ConverterHomePage(),
    );
  }
}

class G723ConverterHomePage extends StatefulWidget {
  const G723ConverterHomePage({super.key});

  @override
  State<G723ConverterHomePage> createState() => _G723ConverterHomePageState();
}

class _G723ConverterHomePageState extends State<G723ConverterHomePage> {
  ConversionMode _selectedMode = ConversionMode.pcmToG723_53k;

  final TextEditingController _sourcePathController = TextEditingController();
  final TextEditingController _destDirController = TextEditingController();
  final TextEditingController _destFilenameController = TextEditingController();

  bool _isConverting = false;
  double _progress = 0.0;
  String _statusText = '';
  ConversionResult? _lastResult;
  G723CancellationToken? _cancellationToken;

  @override
  void initState() {
    super.initState();
    _initDefaultPaths();
  }

  @override
  void dispose() {
    _cancellationToken?.cancel();
    _sourcePathController.dispose();
    _destDirController.dispose();
    _destFilenameController.dispose();
    super.dispose();
  }

  Future<void> _initDefaultPaths() async {
    try {
      final docDir = await getApplicationDocumentsDirectory();
      if (_destDirController.text.isEmpty) {
        _destDirController.text = docDir.path;
      }
      _updateDefaultDestinationFilename();
    } catch (_) {}
  }

  void _updateDefaultDestinationFilename() {
    final source = _sourcePathController.text.trim();
    final baseName = source.isNotEmpty
        ? p.basenameWithoutExtension(source)
        : 'sample_audio';

    switch (_selectedMode) {
      case ConversionMode.pcmToG723_53k:
        _destFilenameController.text = '${baseName}_5.3k.g723';
        break;
      case ConversionMode.pcmToG723_63k:
        _destFilenameController.text = '${baseName}_6.3k.g723';
        break;
      case ConversionMode.g723ToPcm8k:
        _destFilenameController.text = '${baseName}_decoded_8k.wav';
        break;
      case ConversionMode.g723ToPcm16k:
        _destFilenameController.text = '${baseName}_decoded_16k.wav';
        break;
      case ConversionMode.roundtrip53k:
        _destFilenameController.text = '${baseName}_roundtrip_5.3k.wav';
        break;
      case ConversionMode.roundtrip63k:
        _destFilenameController.text = '${baseName}_roundtrip_6.3k.wav';
        break;
    }
  }

  Future<void> _pickSourceFile() async {
    try {
      final allowedExts = _selectedMode.sourceIsWav
          ? ['wav']
          : ['g723', 'bin', 'raw'];

      final file = await FilePicker.pickFile(
        type: FileType.custom,
        allowedExtensions: allowedExts,
      );

      if (file != null && file.path != null) {
        final path = file.path!;
        setState(() {
          _sourcePathController.text = path;
          if (_destDirController.text.isEmpty) {
            _destDirController.text = p.dirname(path);
          }
          _updateDefaultDestinationFilename();
        });
      }
    } catch (e) {
      _showSnackbar('Error picking file: $e', isError: true);
    }
  }

  Future<void> _pickDestinationDirectory() async {
    try {
      final selectedDir = await FilePicker.getDirectoryPath(
        initialDirectory: _destDirController.text.isNotEmpty
            ? _destDirController.text
            : null,
      );

      if (selectedDir != null) {
        setState(() {
          _destDirController.text = selectedDir;
        });
      }
    } catch (e) {
      _showSnackbar('Error picking destination folder: $e', isError: true);
    }
  }

  Future<void> _generateSampleWav(int sampleRate) async {
    try {
      final docDir = await getApplicationDocumentsDirectory();
      final filename = 'sample_${sampleRate == 16000 ? "16k" : "8k"}.wav';
      final file = File(p.join(docDir.path, filename));

      final audio = WavAudio.generateTestSignal(
        sampleRate: sampleRate,
        durationSeconds: 3.0,
      );
      final wavBytes = WavAudio.writeWav(
        samples: audio.samples,
        sampleRate: sampleRate,
      );
      await file.writeAsBytes(wavBytes);

      setState(() {
        _sourcePathController.text = file.path;
        if (_destDirController.text.isEmpty) {
          _destDirController.text = docDir.path;
        }
        _updateDefaultDestinationFilename();
      });

      _showSnackbar('Generated 3.0s sample WAV at $sampleRate Hz: ${file.path}');
    } catch (e) {
      _showSnackbar('Failed to generate sample WAV: $e', isError: true);
    }
  }

  Future<void> _performConversion() async {
    final source = _sourcePathController.text.trim();
    final destDir = _destDirController.text.trim();
    final destName = _destFilenameController.text.trim();

    if (source.isEmpty) {
      _showSnackbar('Please select a source file.', isError: true);
      return;
    }
    if (destDir.isEmpty) {
      _showSnackbar('Please specify a destination folder.', isError: true);
      return;
    }
    if (destName.isEmpty) {
      _showSnackbar('Please specify a destination filename.', isError: true);
      return;
    }

    final destinationPath = p.join(destDir, destName);

    final token = G723CancellationToken();
    _cancellationToken = token;

    setState(() {
      _isConverting = true;
      _progress = 0.0;
      _statusText = 'Starting background conversion...';
      _lastResult = null;
    });

    try {
      final result = await ConverterService.convert(
        mode: _selectedMode,
        sourcePath: source,
        destinationPath: destinationPath,
        cancellationToken: token,
        onProgress: (prog, status) {
          if (!mounted) return;
          setState(() {
            _progress = prog;
            _statusText = status;
          });
        },
      );

      if (!mounted) return;
      setState(() {
        _isConverting = false;
        _cancellationToken = null;
        _lastResult = result;
      });

      if (result.success) {
        _showSnackbar('Conversion completed successfully!');
      } else {
        _showSnackbar(result.errorMessage ?? 'Conversion failed.', isError: true);
      }
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _isConverting = false;
        _cancellationToken = null;
      });
      _showSnackbar('Unexpected conversion error: $e', isError: true);
    }
  }

  void _cancelConversion() {
    if (_cancellationToken != null && !_cancellationToken!.isCancelled) {
      _cancellationToken!.cancel();
      _showSnackbar('Cancelling conversion...');
    }
  }

  void _showSnackbar(String msg, {bool isError = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(msg),
        backgroundColor: isError
            ? Theme.of(context).colorScheme.error
            : Theme.of(context).colorScheme.primary,
        duration: const Duration(seconds: 4),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final info = G723Codec.info;

    return Scaffold(
      appBar: AppBar(
        title: const Text('G.723.1 Converter'),
        actions: [
          Container(
            margin: const EdgeInsets.only(right: 16),
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
            decoration: BoxDecoration(
              color: theme.colorScheme.primaryContainer,
              borderRadius: BorderRadius.circular(16),
            ),
            child: Text(
              'Backend: ${info.backendName} (v${info.abiVersion})',
              style: theme.textTheme.labelMedium?.copyWith(
                color: theme.colorScheme.onPrimaryContainer,
                fontWeight: FontWeight.bold,
              ),
            ),
          ),
        ],
      ),
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 850),
          child: ListView(
            padding: const EdgeInsets.all(20),
            children: [
              // Mode Selection Card
              _buildModeSection(theme),
              const SizedBox(height: 16),

              // Source File Card
              _buildSourceFileSection(theme),
              const SizedBox(height: 16),

              // Destination Card
              _buildDestinationSection(theme),
              const SizedBox(height: 24),

              // Convert Button & Progress
              _buildActionSection(theme),
              const SizedBox(height: 24),

              // Results Card
              if (_lastResult != null) ...[
                _buildResultsSection(theme, _lastResult!),
                const SizedBox(height: 24),
              ],

              // Codec Info Footer
              _buildCodecInfoSection(theme, info),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildModeSection(ThemeData theme) {
    return Card(
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(Icons.swap_horiz, color: theme.colorScheme.primary),
                const SizedBox(width: 8),
                Text(
                  'Conversion Mode',
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: ConversionMode.values.map((mode) {
                final isSelected = mode == _selectedMode;
                return ChoiceChip(
                  label: Text(mode.label),
                  selected: isSelected,
                  onSelected: (selected) {
                    if (selected) {
                      setState(() {
                        _selectedMode = mode;
                        _updateDefaultDestinationFilename();
                      });
                    }
                  },
                );
              }).toList(),
            ),
            const SizedBox(height: 12),
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: theme.colorScheme.surfaceContainerHighest.withValues(alpha: 0.5),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Text(
                _selectedMode.description,
                style: theme.textTheme.bodyMedium?.copyWith(
                  color: theme.colorScheme.onSurfaceVariant,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildSourceFileSection(ThemeData theme) {
    return Card(
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(Icons.audiotrack, color: theme.colorScheme.primary),
                const SizedBox(width: 8),
                Text(
                  _selectedMode.sourceIsWav ? 'Source WAV File' : 'Source G.723.1 File',
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _sourcePathController,
                    decoration: InputDecoration(
                      hintText: _selectedMode.sourceIsWav
                          ? 'Select or enter path to 16-bit PCM WAV (8k or 16k)'
                          : 'Select or enter path to G.723.1 bitstream (.g723)',
                      border: const OutlineInputBorder(),
                      isDense: true,
                    ),
                    onChanged: (_) => _updateDefaultDestinationFilename(),
                  ),
                ),
                const SizedBox(width: 8),
                FilledButton.tonalIcon(
                  onPressed: _isConverting ? null : _pickSourceFile,
                  icon: const Icon(Icons.file_open),
                  label: const Text('Browse'),
                ),
              ],
            ),
            if (_selectedMode.sourceIsWav) ...[
              const SizedBox(height: 10),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Quick Test Signals:',
                    style: theme.textTheme.bodySmall?.copyWith(fontWeight: FontWeight.w600),
                  ),
                  const SizedBox(height: 6),
                  Wrap(
                    spacing: 8,
                    runSpacing: 6,
                    children: [
                      OutlinedButton.icon(
                        style: OutlinedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                          visualDensity: VisualDensity.compact,
                        ),
                        onPressed: _isConverting ? null : () => _generateSampleWav(8000),
                        icon: const Icon(Icons.auto_awesome, size: 16),
                        label: const Text('Generate 8 kHz WAV'),
                      ),
                      OutlinedButton.icon(
                        style: OutlinedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                          visualDensity: VisualDensity.compact,
                        ),
                        onPressed: _isConverting ? null : () => _generateSampleWav(16000),
                        icon: const Icon(Icons.auto_awesome, size: 16),
                        label: const Text('Generate 16 kHz WAV'),
                      ),
                    ],
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _buildDestinationSection(ThemeData theme) {
    return Card(
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(Icons.folder, color: theme.colorScheme.primary),
                const SizedBox(width: 8),
                Text(
                  'Destination Output',
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              'Destination Folder:',
              style: theme.textTheme.labelMedium?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 6),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _destDirController,
                    decoration: const InputDecoration(
                      hintText: 'Directory where output file will be saved',
                      border: OutlineInputBorder(),
                      isDense: true,
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                FilledButton.tonalIcon(
                  onPressed: _isConverting ? null : _pickDestinationDirectory,
                  icon: const Icon(Icons.folder_open),
                  label: const Text('Browse'),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              'Destination Filename:',
              style: theme.textTheme.labelMedium?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 6),
            TextField(
              controller: _destFilenameController,
              decoration: InputDecoration(
                hintText: 'Output filename (e.g. converted${_selectedMode.defaultExt})',
                border: const OutlineInputBorder(),
                isDense: true,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildActionSection(ThemeData theme) {
    return Column(
      children: [
        SizedBox(
          width: double.infinity,
          height: 52,
          child: FilledButton.icon(
            onPressed: _isConverting ? null : _performConversion,
            icon: _isConverting
                ? const SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(
                      strokeWidth: 2.5,
                      color: Colors.white,
                    ),
                  )
                : const Icon(Icons.transform),
            label: Text(
              _isConverting ? 'Converting in Background...' : 'Perform Conversion',
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
          ),
        ),
        if (_isConverting) ...[
          const SizedBox(height: 16),
          LinearProgressIndicator(value: _progress > 0 ? _progress : null),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: Text(
                  _statusText,
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: theme.colorScheme.primary,
                  ),
                ),
              ),
              const SizedBox(width: 8),
              OutlinedButton.icon(
                onPressed: _cancelConversion,
                icon: const Icon(Icons.cancel, size: 18),
                label: const Text('Cancel'),
                style: OutlinedButton.styleFrom(
                  foregroundColor: theme.colorScheme.error,
                  visualDensity: VisualDensity.compact,
                ),
              ),
            ],
          ),
        ],
      ],
    );
  }

  Widget _buildResultsSection(ThemeData theme, ConversionResult result) {
    if (!result.success) {
      return Card(
        color: theme.colorScheme.errorContainer,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(Icons.error, color: theme.colorScheme.error),
                  const SizedBox(width: 8),
                  Text(
                    'Conversion Failed',
                    style: theme.textTheme.titleMedium?.copyWith(
                      color: theme.colorScheme.onErrorContainer,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 8),
              Text(
                result.errorMessage ?? 'Unknown error occurred.',
                style: TextStyle(color: theme.colorScheme.onErrorContainer),
              ),
            ],
          ),
        ),
      );
    }

    return Card(
      color: theme.colorScheme.primaryContainer.withValues(alpha: 0.3),
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.check_circle, color: Colors.green, size: 28),
                const SizedBox(width: 8),
                Text(
                  'Conversion Successful',
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),
            const Divider(height: 24),
            Wrap(
              spacing: 24,
              runSpacing: 12,
              children: [
                _metricItem(theme, 'Duration', '${result.durationSeconds.toStringAsFixed(2)} s'),
                _metricItem(theme, 'Frames', '${result.framesProcessed}'),
                _metricItem(theme, 'Input Size', _formatBytes(result.inputBytes)),
                _metricItem(theme, 'Output Size', _formatBytes(result.outputBytes)),
                if (result.compressionRatio > 0)
                  _metricItem(
                    theme,
                    'Ratio',
                    '${result.compressionRatio.toStringAsFixed(1)}x',
                  ),
              ],
            ),
            if (result.frameTypeCounts.isNotEmpty) ...[
              const SizedBox(height: 12),
              Text(
                'Frame Breakdown: ${result.frameTypeCounts.entries.map((e) => '${e.key}: ${e.value}').join(', ')}',
                style: theme.textTheme.bodySmall,
              ),
            ],
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: theme.colorScheme.surface,
                borderRadius: BorderRadius.circular(6),
                border: Border.all(color: theme.colorScheme.outlineVariant),
              ),
              child: Row(
                children: [
                  const Icon(Icons.file_present, size: 20),
                  const SizedBox(width: 8),
                  Expanded(
                    child: SelectableText(
                      result.destinationPath,
                      style: theme.textTheme.bodySmall?.copyWith(fontFamily: 'monospace'),
                    ),
                  ),
                  IconButton(
                    icon: const Icon(Icons.copy, size: 18),
                    tooltip: 'Copy Path',
                    onPressed: () {
                      Clipboard.setData(ClipboardData(text: result.destinationPath));
                      _showSnackbar('File path copied to clipboard');
                    },
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _metricItem(ThemeData theme, String title, String value) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(title, style: theme.textTheme.labelSmall),
        Text(
          value,
          style: theme.textTheme.titleMedium?.copyWith(
            fontWeight: FontWeight.bold,
            color: theme.colorScheme.primary,
          ),
        ),
      ],
    );
  }

  Widget _buildCodecInfoSection(ThemeData theme, G723CodecInfo info) {
    return Card(
      elevation: 1,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Native G.723.1 Engine Info',
              style: theme.textTheme.titleSmall?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            Text(
              'Backend: ${info.backendName} | ABI Version: ${info.abiVersion} | '
              '5.3 kbps: ${info.supports53k ? "Supported" : "No"} | '
              '6.3 kbps: ${info.supports63k ? "Supported" : "No"}',
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
            const SizedBox(height: 4),
            Text(
              'Supported PCM rates: 8000 Hz (240 samples/frame) & 16000 Hz (480 samples/frame)',
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
    );
  }

  static String _formatBytes(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(2)} MB';
  }
}
