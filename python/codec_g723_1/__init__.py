"""Python bindings for the ITU-T G.723.1 speech codec."""

from .codec import (
    G723Bitrate,
    G723FrameType,
    G723CodecException,
    G723Encoder,
    G723Decoder,
    inspect_frame,
    encode_wav_to_g723,
    decode_g723_to_wav,
    get_abi_version,
    get_backend_name,
    supports_bitrate,
)

__version__ = "0.1.0"
__all__ = [
    "G723Bitrate",
    "G723FrameType",
    "G723CodecException",
    "G723Encoder",
    "G723Decoder",
    "inspect_frame",
    "encode_wav_to_g723",
    "decode_g723_to_wav",
    "get_abi_version",
    "get_backend_name",
    "supports_bitrate",
]
