import os
import sys
import ctypes
from ctypes import (
    c_char_p,
    c_int16,
    c_int32,
    c_uint8,
    c_uint32,
    c_void_p,
    POINTER,
    byref,
)

_lib = None


def find_library_path() -> str:
    """Locates the compiled g723_1 shared library."""
    # 1. Check explicit environment variable
    env_path = os.environ.get("G723_LIB_PATH")
    if env_path and os.path.isfile(env_path):
        return env_path

    # Determine standard library filenames by OS
    if sys.platform == "win32":
        lib_names = ["g723_1.dll", "libg723_1.dll"]
    elif sys.platform == "darwin":
        lib_names = ["libg723_1.dylib", "g723_1.dylib"]
    else:
        lib_names = ["libg723_1.so", "g723_1.so"]

    current_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(current_dir, "..", ".."))

    search_dirs = [
        current_dir,
        os.path.join(current_dir, "lib"),
        project_root,
        os.path.join(project_root, "build"),
        os.path.join(project_root, "build_cmake"),
        os.path.join(project_root, "build_cmake", "Release"),
        os.path.join(project_root, "build_cmake", "Debug"),
    ]

    for directory in search_dirs:
        for name in lib_names:
            candidate = os.path.join(directory, name)
            if os.path.isfile(candidate):
                return candidate

    # Try system loader via ctypes.util
    import ctypes.util
    sys_found = ctypes.util.find_library("g723_1")
    if sys_found:
        return sys_found

    raise FileNotFoundError(
        "Could not find the compiled g723_1 native shared library.\n"
        "Please compile it using CMake:\n"
        "    cmake -B build && cmake --build build\n"
        "Or set the G723_LIB_PATH environment variable to the path of libg723_1.so / .dll / .dylib."
    )


def get_lib():
    """Loads and caches the native C library bindings."""
    global _lib
    if _lib is not None:
        return _lib

    lib_path = find_library_path()
    lib = ctypes.CDLL(lib_path)

    # Info APIs
    lib.g723_abi_version.argtypes = []
    lib.g723_abi_version.restype = c_uint32

    lib.g723_backend_name.argtypes = []
    lib.g723_backend_name.restype = c_char_p

    lib.g723_backend_supports_bitrate.argtypes = [c_int32]
    lib.g723_backend_supports_bitrate.restype = c_int32

    # Encoder APIs
    lib.g723_encoder_create.argtypes = [c_int32, POINTER(c_void_p)]
    lib.g723_encoder_create.restype = c_int32

    lib.g723_encoder_destroy.argtypes = [c_void_p]
    lib.g723_encoder_destroy.restype = None

    lib.g723_encoder_set_sample_rate.argtypes = [c_void_p, c_uint32]
    lib.g723_encoder_set_sample_rate.restype = c_int32

    lib.g723_encode_frame.argtypes = [
        c_void_p,
        POINTER(c_int16),
        c_uint32,
        POINTER(c_uint8),
        c_uint32,
        POINTER(c_uint32),
    ]
    lib.g723_encode_frame.restype = c_int32

    # Decoder APIs
    lib.g723_decoder_create.argtypes = [POINTER(c_void_p)]
    lib.g723_decoder_create.restype = c_int32

    lib.g723_decoder_destroy.argtypes = [c_void_p]
    lib.g723_decoder_destroy.restype = None

    lib.g723_decoder_set_sample_rate.argtypes = [c_void_p, c_uint32]
    lib.g723_decoder_set_sample_rate.restype = c_int32

    lib.g723_decode_frame.argtypes = [
        c_void_p,
        POINTER(c_uint8),
        c_uint32,
        POINTER(c_int16),
        c_uint32,
        POINTER(c_uint32),
    ]
    lib.g723_decode_frame.restype = c_int32

    # Frame Inspection APIs
    lib.g723_frame_type.argtypes = [POINTER(c_uint8), c_uint32]
    lib.g723_frame_type.restype = c_int32

    lib.g723_frame_size.argtypes = [c_int32]
    lib.g723_frame_size.restype = c_uint32

    _lib = lib
    return _lib
