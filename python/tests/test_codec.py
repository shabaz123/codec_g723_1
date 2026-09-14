import array
import math
import os
import sys
import tempfile
import unittest
import wave

# Add python directory to sys.path so codec_g723_1 can be imported directly
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from codec_g723_1 import (
    G723Bitrate,
    G723FrameType,
    G723Encoder,
    G723Decoder,
    get_abi_version,
    get_backend_name,
    supports_bitrate,
    inspect_frame,
    encode_wav_to_g723,
    decode_g723_to_wav,
)


class TestG723Codec(unittest.TestCase):
    def test_metadata(self):
        self.assertEqual(get_abi_version(), 1)
        self.assertEqual(get_backend_name(), "c")
        self.assertTrue(supports_bitrate(G723Bitrate.KBPS_53))
        self.assertTrue(supports_bitrate(G723Bitrate.KBPS_63))
        self.assertFalse(supports_bitrate(8000))

    def test_frame_inspection(self):
        frame_63 = bytes([0x00] * 24)
        ft, size = inspect_frame(frame_63)
        self.assertEqual(ft, G723FrameType.RATE_6300)
        self.assertEqual(size, 24)

        frame_53 = bytes([0x01] * 20)
        ft, size = inspect_frame(frame_53)
        self.assertEqual(ft, G723FrameType.RATE_5300)
        self.assertEqual(size, 20)

        frame_sid = bytes([0x02] * 4)
        ft, size = inspect_frame(frame_sid)
        self.assertEqual(ft, G723FrameType.SID)
        self.assertEqual(size, 4)

        ft, size = inspect_frame(b"")
        self.assertEqual(ft, G723FrameType.INVALID)
        self.assertEqual(size, 0)

    def test_roundtrip_8k_63k(self):
        num_frames = 6
        samples_per_frame = 240
        total_samples = num_frames * samples_per_frame

        input_samples = array.array('h', [0] * total_samples)
        for i in range(total_samples):
            t = i / 8000.0
            val = 8000.0 * math.sin(2.0 * math.pi * 300.0 * t) + 4000.0 * math.sin(2.0 * math.pi * 600.0 * t)
            input_samples[i] = int(max(-32768, min(32767, round(val))))

        decoded_samples = array.array('h')

        with G723Encoder(bitrate=G723Bitrate.KBPS_63, sample_rate=8000) as enc, \
             G723Decoder(sample_rate=8000) as dec:

            for f in range(num_frames):
                frame_pcm = input_samples[f * samples_per_frame : (f + 1) * samples_per_frame]
                encoded = enc.encode_frame(frame_pcm)
                self.assertEqual(len(encoded), 24)

                pcm_bytes = dec.decode_frame(encoded)
                self.assertEqual(len(pcm_bytes), 480)
                decoded_frame = array.array('h', pcm_bytes)
                decoded_samples.extend(decoded_frame)

        self.assertEqual(len(decoded_samples), total_samples)
        energy = sum(s * s for s in decoded_samples)
        self.assertGreater(energy, 0)

    def test_roundtrip_8k_53k(self):
        num_frames = 6
        samples_per_frame = 240
        total_samples = num_frames * samples_per_frame

        input_samples = array.array('h', [0] * total_samples)
        for i in range(total_samples):
            t = i / 8000.0
            val = 8000.0 * math.sin(2.0 * math.pi * 400.0 * t)
            input_samples[i] = int(max(-32768, min(32767, round(val))))

        decoded_samples = array.array('h')

        with G723Encoder(bitrate=G723Bitrate.KBPS_53, sample_rate=8000) as enc, \
             G723Decoder(sample_rate=8000) as dec:

            for f in range(num_frames):
                frame_pcm = input_samples[f * samples_per_frame : (f + 1) * samples_per_frame]
                encoded = enc.encode_frame(frame_pcm)
                self.assertEqual(len(encoded), 20)

                pcm_bytes = dec.decode_frame(encoded)
                decoded_frame = array.array('h', pcm_bytes)
                decoded_samples.extend(decoded_frame)

        self.assertEqual(len(decoded_samples), total_samples)
        energy = sum(s * s for s in decoded_samples)
        self.assertGreater(energy, 0)

    def test_roundtrip_16k(self):
        samples_per_frame = 480
        input_pcm = array.array('h', [0] * samples_per_frame)
        for i in range(samples_per_frame):
            val = 8000.0 * math.sin(2.0 * math.pi * 300.0 * i / 16000.0)
            input_pcm[i] = int(max(-32768, min(32767, round(val))))

        with G723Encoder(bitrate=G723Bitrate.KBPS_63, sample_rate=16000) as enc, \
             G723Decoder(sample_rate=16000) as dec:

            encoded = enc.encode_frame(input_pcm)
            self.assertEqual(len(encoded), 24)

            decoded_bytes = dec.decode_frame(encoded)
            self.assertEqual(len(decoded_bytes), 960) # 480 * 2

    def test_wav_conversion(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            wav_in = os.path.join(tmpdir, "input.wav")
            g723_file = os.path.join(tmpdir, "encoded.g723")
            wav_out = os.path.join(tmpdir, "output.wav")

            # Generate 1.0 second 8 kHz mono audio
            samples = array.array('h', [
                int(max(-32768, min(32767, round(7000.0 * math.sin(2.0 * math.pi * 440.0 * i / 8000.0)))))
                for i in range(8000)
            ])

            with wave.open(wav_in, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(8000)
                wf.writeframes(samples.tobytes())

            # Encode to G.723.1
            frames_enc = encode_wav_to_g723(wav_in, g723_file, bitrate=G723Bitrate.KBPS_63)
            self.assertGreater(frames_enc, 0)
            self.assertTrue(os.path.isfile(g723_file))
            self.assertEqual(os.path.getsize(g723_file), frames_enc * 24)

            # Decode to WAV
            frames_dec = decode_g723_to_wav(g723_file, wav_out, sample_rate=8000)
            self.assertEqual(frames_dec, frames_enc)
            self.assertTrue(os.path.isfile(wav_out))

            with wave.open(wav_out, "rb") as wf:
                self.assertEqual(wf.getnchannels(), 1)
                self.assertEqual(wf.getsampwidth(), 2)
                self.assertEqual(wf.getframerate(), 8000)
                self.assertEqual(wf.getnframes(), frames_dec * 240)


if __name__ == "__main__":
    unittest.main()
