import array
import enum
import os
import wave
from ctypes import (
    c_int16,
    c_uint8,
    c_uint32,
    c_void_p,
    byref,
    string_at,
)
from typing import List, Sequence, Tuple, Union

from .bindings import get_lib


class G723Bitrate(enum.IntEnum):
    KBPS_53 = 5300
    KBPS_63 = 6300


class G723FrameType(enum.IntEnum):
    RATE_6300 = 0
    RATE_5300 = 1
    SID = 2
    UNTRANSMITTED = 3
    INVALID = 255


class G723CodecException(Exception):
    def __init__(self, message: str, native_code: int = None):
        super().__init__(message)
        self.native_code = native_code


def get_abi_version() -> int:
    return get_lib().g723_abi_version()


def get_backend_name() -> str:
    name_bytes = get_lib().g723_backend_name()
    return name_bytes.decode("utf-8") if name_bytes else "unknown"


def supports_bitrate(bitrate: Union[int, G723Bitrate]) -> bool:
    val = int(bitrate)
    return get_lib().g723_backend_supports_bitrate(val) != 0


def inspect_frame(frame: bytes) -> Tuple[G723FrameType, int]:
    """Inspects a G.723.1 frame and returns (frame_type, expected_size_in_bytes)."""
    if not frame:
        return (G723FrameType.INVALID, 0)

    lib = get_lib()
    buf = (c_uint8 * len(frame)).from_buffer_copy(frame)
    raw_type = lib.g723_frame_type(buf, len(frame))

    try:
        frame_type = G723FrameType(raw_type)
    except ValueError:
        frame_type = G723FrameType.INVALID

    expected_size = lib.g723_frame_size(raw_type)
    return (frame_type, expected_size)


class G723Encoder:
    """ITU-T G.723.1 Speech Encoder.

    Encodes 16-bit linear PCM audio into 6.3 kbps (24 bytes) or 5.3 kbps (20 bytes) frames.
    Supports 8000 Hz (240 samples/frame) and 16000 Hz (480 samples/frame) PCM.
    """

    def __init__(self, bitrate: Union[int, G723Bitrate] = G723Bitrate.KBPS_63, sample_rate: int = 8000):
        if sample_rate not in (8000, 16000):
            raise ValueError(f"Sample rate must be either 8000 or 16000 Hz, got {sample_rate}")

        self.bitrate = G723Bitrate(bitrate)
        self._sample_rate = sample_rate
        self._handle = c_void_p()
        self._closed = False

        lib = get_lib()
        res = lib.g723_encoder_create(int(self.bitrate), byref(self._handle))
        if res != 0 or not self._handle:
            raise G723CodecException(f"Failed to create G.723.1 encoder (code {res})", res)

        if sample_rate != 8000:
            res_sr = lib.g723_encoder_set_sample_rate(self._handle, sample_rate)
            if res_sr != 0:
                self.close()
                raise G723CodecException(f"Failed to set sample rate {sample_rate} (code {res_sr})", res_sr)

    @property
    def sample_rate(self) -> int:
        return self._sample_rate

    @property
    def samples_per_frame(self) -> int:
        return 480 if self._sample_rate == 16000 else 240

    def encode_frame(self, pcm_samples: Union[bytes, Sequence[int], array.array]) -> bytes:
        """Encodes one frame of PCM audio into a G.723.1 frame (24 or 20 bytes)."""
        if self._closed:
            raise RuntimeError("G723Encoder is closed.")

        expected_count = self.samples_per_frame

        if isinstance(pcm_samples, (bytes, bytearray)):
            if len(pcm_samples) != expected_count * 2:
                raise ValueError(f"Expected {expected_count * 2} PCM bytes ({expected_count} samples), got {len(pcm_samples)}")
            c_pcm = (c_int16 * expected_count).from_buffer_copy(pcm_samples)
        elif isinstance(pcm_samples, array.array) and pcm_samples.typecode == 'h':
            if len(pcm_samples) != expected_count:
                raise ValueError(f"Expected {expected_count} PCM samples, got {len(pcm_samples)}")
            c_pcm = (c_int16 * expected_count).from_buffer(pcm_samples)
        else:
            if len(pcm_samples) != expected_count:
                raise ValueError(f"Expected {expected_count} PCM samples, got {len(pcm_samples)}")
            c_pcm = (c_int16 * expected_count)(*pcm_samples)

        out_buf = (c_uint8 * 24)()
        out_size = c_uint32(0)

        lib = get_lib()
        res = lib.g723_encode_frame(
            self._handle,
            c_pcm,
            expected_count,
            out_buf,
            len(out_buf),
            byref(out_size),
        )

        if res != 0:
            raise G723CodecException(f"Failed to encode G.723.1 frame (code {res})", res)

        return bytes(out_buf[: out_size.value])

    def close(self):
        if not self._closed:
            if self._handle:
                get_lib().g723_encoder_destroy(self._handle)
                self._handle = c_void_p()
            self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


class G723Decoder:
    """ITU-T G.723.1 Speech Decoder.

    Decodes G.723.1 bitstream frames into 16-bit linear PCM audio.
    Supports decoding to 8000 Hz (240 samples/frame) or 16000 Hz (480 samples/frame).
    """

    def __init__(self, sample_rate: int = 8000):
        if sample_rate not in (8000, 16000):
            raise ValueError(f"Sample rate must be either 8000 or 16000 Hz, got {sample_rate}")

        self._sample_rate = sample_rate
        self._handle = c_void_p()
        self._closed = False

        lib = get_lib()
        res = lib.g723_decoder_create(byref(self._handle))
        if res != 0 or not self._handle:
            raise G723CodecException(f"Failed to create G.723.1 decoder (code {res})", res)

        if sample_rate != 8000:
            res_sr = lib.g723_decoder_set_sample_rate(self._handle, sample_rate)
            if res_sr != 0:
                self.close()
                raise G723CodecException(f"Failed to set sample rate {sample_rate} (code {res_sr})", res_sr)

    @property
    def sample_rate(self) -> int:
        return self._sample_rate

    @property
    def samples_per_frame(self) -> int:
        return 480 if self._sample_rate == 16000 else 240

    def decode_frame(self, frame_bytes: bytes) -> bytes:
        """Decodes one G.723.1 frame into 16-bit linear PCM byte buffer."""
        if self._closed:
            raise RuntimeError("G723Decoder is closed.")

        if not frame_bytes:
            raise ValueError("Encoded frame bytes cannot be empty.")

        expected_samples = self.samples_per_frame
        in_buf = (c_uint8 * len(frame_bytes)).from_buffer_copy(frame_bytes)
        pcm_buf = (c_int16 * expected_samples)()
        pcm_samples_out = c_uint32(0)

        lib = get_lib()
        res = lib.g723_decode_frame(
            self._handle,
            in_buf,
            len(frame_bytes),
            pcm_buf,
            expected_samples,
            byref(pcm_samples_out),
        )

        if res != 0:
            raise G723CodecException(f"Failed to decode G.723.1 frame (code {res})", res)

        return string_at(byref(pcm_buf), pcm_samples_out.value * 2)

    def close(self):
        if not self._closed:
            if self._handle:
                get_lib().g723_decoder_destroy(self._handle)
                self._handle = c_void_p()
            self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


def encode_wav_to_g723(
    wav_path: str,
    g723_path: str,
    bitrate: Union[int, G723Bitrate] = G723Bitrate.KBPS_63,
    progress_callback=None,
) -> int:
    """Encodes a 16-bit Mono WAV file (8 kHz or 16 kHz) into a G.723.1 bitstream file.

    Returns the number of frames processed.
    """
    with wave.open(wav_path, "rb") as wf:
        n_channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        sample_rate = wf.getframerate()
        n_frames = wf.getnframes()

        if sampwidth != 2:
            raise ValueError(f"Only 16-bit PCM WAV is supported (got {sampwidth * 8}-bit).")
        if sample_rate not in (8000, 16000):
            raise ValueError(f"Sample rate must be 8000 Hz or 16000 Hz (got {sample_rate} Hz).")

        raw_pcm = wf.readframes(n_frames)

    # Downmix stereo to mono if needed
    if n_channels == 2:
        samples_in = array.array('h', raw_pcm)
        mono_samples = array.array('h', [
            max(-32768, min(32767, (samples_in[i] + samples_in[i + 1]) // 2))
            for i in range(0, len(samples_in), 2)
        ])
        raw_pcm = mono_samples.tobytes()
    elif n_channels != 1:
        raise ValueError(f"Only mono or stereo WAV files are supported (got {n_channels} channels).")

    with G723Encoder(bitrate=bitrate, sample_rate=sample_rate) as encoder:
        samples_per_frame = encoder.samples_per_frame
        bytes_per_frame = samples_per_frame * 2
        total_frames = (len(raw_pcm) + bytes_per_frame - 1) // bytes_per_frame

        with open(g723_path, "wb") as out_f:
            frames_processed = 0
            for offset in range(0, len(raw_pcm), bytes_per_frame):
                chunk = raw_pcm[offset : offset + bytes_per_frame]
                if len(chunk) < bytes_per_frame:
                    chunk = chunk + b"\x00" * (bytes_per_frame - len(chunk))

                encoded = encoder.encode_frame(chunk)
                out_f.write(encoded)
                frames_processed += 1

                if progress_callback and (frames_processed % 32 == 0 or frames_processed == total_frames):
                    progress_callback(frames_processed, total_frames)

        return frames_processed


def decode_g723_to_wav(
    g723_path: str,
    wav_path: str,
    sample_rate: int = 8000,
    progress_callback=None,
) -> int:
    """Decodes a G.723.1 bitstream file into a canonical 16-bit Mono WAV file.

    Returns the number of frames processed.
    """
    with open(g723_path, "rb") as f:
        data = f.read()

    with G723Decoder(sample_rate=sample_rate) as decoder:
        pcm_chunks = []
        offset = 0
        frames_processed = 0
        total_bytes = len(data)

        while offset < total_bytes:
            first_byte = data[offset]
            rate_bits = first_byte & 0x03
            frame_size = {
                0: 24, # 6.3k
                1: 20, # 5.3k
                2: 4,  # SID
                3: 1,  # Untransmitted
            }.get(rate_bits, 0)

            if frame_size == 0 or offset + frame_size > total_bytes:
                break

            frame = data[offset : offset + frame_size]
            pcm = decoder.decode_frame(frame)
            pcm_chunks.append(pcm)

            offset += frame_size
            frames_processed += 1

            if progress_callback and frames_processed % 32 == 0:
                progress_callback(offset, total_bytes)

        pcm_all = b"".join(pcm_chunks)
        with wave.open(wav_path, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(sample_rate)
            wf.writeframes(pcm_all)

        return frames_processed
