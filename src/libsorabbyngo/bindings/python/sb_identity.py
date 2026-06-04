"""
sb_identity.py — Python bindings for libsb_identity

Uses ctypes to wrap the compiled libsorabbyngo.so.
Drop-in for Novela and any other Sorabbyngo Python application.

Usage:
    from sb_identity import Identity

    node = Identity()
    print(node.nodeid)          # full 64-char hex NodeID
    print(node.short_id)        # first 8 chars — for prompts/logs
    print(node.source)          # 'kernel' or 'userspace'

    sig = node.sign(b"some payload")
    node.verify(b"some payload", sig, node.pubkey)  # raises on failure

Sorabbyngo Labs — Kisii, Kenya
"""

import ctypes
import ctypes.util
import os
import sys
from pathlib import Path

# ── Constants (mirror identity.h) ──────────────────────────────────────────

SB_NODEID_LEN        = 64
SB_NODEID_BYTES      = 32
SB_ED25519_PUBKEY_LEN = 32
SB_ED25519_SECKEY_LEN = 64
SB_ED25519_SIG_LEN    = 64

SB_OK                   =  0
SB_ERR_NODEID_UNAVAIL   = -1
SB_ERR_CRYPTO           = -2
SB_ERR_INVALID_ARG      = -3
SB_ERR_BUFFER_TOO_SMALL = -4
SB_ERR_IO               = -5

SB_NODEID_SRC_KERNEL    = 0
SB_NODEID_SRC_USERSPACE = 1

# ── Load shared library ─────────────────────────────────────────────────────

def _load_lib():
    """Find and load libsorabbyngo.so from standard paths or build dir."""
    search_paths = [
        # Build directory (dev workflow)
        Path(__file__).parent.parent / "build" / "libsorabbyngo.so",
        Path(__file__).parent.parent / "build" / "libsorabbyngo.so.0",
        # Installed system path
        Path("/usr/local/lib/libsorabbyngo.so"),
        Path("/usr/lib/libsorabbyngo.so"),
    ]

    # Also check LD_LIBRARY_PATH
    ld_path = os.environ.get("LD_LIBRARY_PATH", "")
    for d in ld_path.split(":"):
        if d:
            search_paths.insert(0, Path(d) / "libsorabbyngo.so")

    for path in search_paths:
        if path.exists():
            return ctypes.CDLL(str(path))

    # Last resort: let the linker find it
    name = ctypes.util.find_library("sorabbyngo")
    if name:
        return ctypes.CDLL(name)

    raise OSError(
        "libsorabbyngo.so not found. "
        "Build first: cd build && cmake .. && make, "
        "or set LD_LIBRARY_PATH to the build directory."
    )


_lib = _load_lib()

# ── Function signatures ─────────────────────────────────────────────────────

_lib.sb_identity_init.restype  = ctypes.c_int
_lib.sb_identity_init.argtypes = [
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_int),
]

_lib.sb_identity_free.restype  = None
_lib.sb_identity_free.argtypes = [ctypes.c_void_p]

_lib.sb_identity_get_nodeid.restype  = ctypes.c_int
_lib.sb_identity_get_nodeid.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_size_t,
]

_lib.sb_identity_get_nodeid_bytes.restype  = ctypes.c_int
_lib.sb_identity_get_nodeid_bytes.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
]

_lib.sb_identity_get_pubkey.restype  = ctypes.c_int
_lib.sb_identity_get_pubkey.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

_lib.sb_identity_sign.restype  = ctypes.c_int
_lib.sb_identity_sign.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_size_t,
    ctypes.c_char_p,
]

_lib.sb_identity_verify_sig.restype  = ctypes.c_int
_lib.sb_identity_verify_sig.argtypes = [
    ctypes.c_char_p,
    ctypes.c_size_t,
    ctypes.c_char_p,
    ctypes.c_char_p,
]

_lib.sb_identity_verify_peer.restype  = ctypes.c_int
_lib.sb_identity_verify_peer.argtypes = [ctypes.c_char_p]

_lib.sb_identity_short.restype  = ctypes.c_int
_lib.sb_identity_short.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

_lib.sb_identity_strerror.restype  = ctypes.c_char_p
_lib.sb_identity_strerror.argtypes = [ctypes.c_int]

# ── Exceptions ──────────────────────────────────────────────────────────────

class SbIdentityError(Exception):
    def __init__(self, code: int, msg: str = ""):
        err_str = _lib.sb_identity_strerror(code).decode()
        super().__init__(f"[{code}] {err_str}" + (f": {msg}" if msg else ""))
        self.code = code


def _check(rc: int, msg: str = ""):
    if rc != SB_OK:
        raise SbIdentityError(rc, msg)

# ── Identity class ──────────────────────────────────────────────────────────

class Identity:
    """
    Represents this node's Sorabbyngo hardware identity.

    Thread safety: not thread-safe. Create one instance per thread
    or protect with a lock.
    """

    def __init__(self):
        self._ctx = ctypes.c_void_p(None)
        ctx_ptr = ctypes.c_void_p(None)
        source  = ctypes.c_int(0)

        rc = _lib.sb_identity_init(
            ctypes.byref(ctx_ptr),
            ctypes.byref(source)
        )
        _check(rc, "sb_identity_init")

        self._ctx   = ctx_ptr
        self._source = source.value

        # Cache frequently-used values
        self._nodeid  = self._read_nodeid()
        self._pubkey  = self._read_pubkey()
        self._short   = self._read_short()

    def __del__(self):
        if self._ctx:
            _lib.sb_identity_free(self._ctx)
            self._ctx = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        if self._ctx:
            _lib.sb_identity_free(self._ctx)
            self._ctx = None

    # ── Properties ───────────────────────────────────────────────────────

    @property
    def nodeid(self) -> str:
        """Full 64-character hex NodeID string."""
        return self._nodeid

    @property
    def nodeid_bytes(self) -> bytes:
        """Raw 32-byte NodeID."""
        buf = ctypes.create_string_buffer(SB_NODEID_BYTES)
        _check(_lib.sb_identity_get_nodeid_bytes(self._ctx, buf))
        return bytes(buf.raw)

    @property
    def pubkey(self) -> bytes:
        """32-byte Ed25519 public key."""
        return self._pubkey

    @property
    def short_id(self) -> str:
        """First 8 hex chars of NodeID — for prompts and log lines."""
        return self._short

    @property
    def source(self) -> str:
        """'kernel' if NodeID came from /proc/sorabbyngo/nodeid, else 'userspace'."""
        return "kernel" if self._source == SB_NODEID_SRC_KERNEL else "userspace"

    # ── Signing & Verification ────────────────────────────────────────────

    def sign(self, payload: bytes) -> bytes:
        """
        Sign payload with this node's Ed25519 secret key.

        Returns 64-byte signature bytes.
        Raises SbIdentityError on failure.
        """
        if not isinstance(payload, (bytes, bytearray)):
            raise TypeError("payload must be bytes")
        sig_buf = ctypes.create_string_buffer(SB_ED25519_SIG_LEN)
        rc = _lib.sb_identity_sign(self._ctx, payload, len(payload), sig_buf)
        _check(rc, "sign")
        return bytes(sig_buf.raw)

    @staticmethod
    def verify(payload: bytes, sig: bytes, peer_pubkey: bytes) -> None:
        """
        Verify a signature from a peer node.

        Raises SbIdentityError if the signature is invalid.
        Returns None on success.
        """
        if len(sig) != SB_ED25519_SIG_LEN:
            raise ValueError(f"sig must be {SB_ED25519_SIG_LEN} bytes")
        if len(peer_pubkey) != SB_ED25519_PUBKEY_LEN:
            raise ValueError(f"peer_pubkey must be {SB_ED25519_PUBKEY_LEN} bytes")
        rc = _lib.sb_identity_verify_sig(
            payload, len(payload), sig, peer_pubkey
        )
        _check(rc, "verify_sig")

    @staticmethod
    def verify_peer_nodeid(nodeid_hex: str) -> bool:
        """
        Check if a hex NodeID string is well-formed and non-zero.
        Returns True if valid, False otherwise.
        """
        rc = _lib.sb_identity_verify_peer(nodeid_hex.encode())
        return rc == SB_OK

    # ── Private helpers ───────────────────────────────────────────────────

    def _read_nodeid(self) -> str:
        buf = ctypes.create_string_buffer(SB_NODEID_LEN + 1)
        _check(_lib.sb_identity_get_nodeid(self._ctx, buf, SB_NODEID_LEN + 1))
        return buf.value.decode()

    def _read_pubkey(self) -> bytes:
        buf = ctypes.create_string_buffer(SB_ED25519_PUBKEY_LEN)
        _check(_lib.sb_identity_get_pubkey(self._ctx, buf))
        return bytes(buf.raw)

    def _read_short(self) -> str:
        buf = ctypes.create_string_buffer(9)
        _check(_lib.sb_identity_short(self._ctx, buf))
        return buf.value.decode()

    def __repr__(self):
        return f"<Identity node={self.short_id}... source={self.source}>"


# ── CLI quick-test ──────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("\n  Sorabbyngo OS — libsb_identity Python binding\n")

    with Identity() as node:
        print(f"  NodeID   : {node.nodeid}")
        print(f"  Short ID : {node.short_id}")
        print(f"  Source   : {node.source}")
        print(f"  PubKey   : {node.pubkey.hex()}")

        payload = b"novela:patient-record:test"
        sig = node.sign(payload)
        print(f"\n  Signed   : {payload}")
        print(f"  Sig      : {sig.hex()[:32]}...")

        Identity.verify(payload, sig, node.pubkey)
        print(f"  Verify   : OK ✓")

        try:
            Identity.verify(b"tampered", sig, node.pubkey)
        except SbIdentityError as e:
            print(f"  Tamper   : Correctly rejected — {e}")

    print()
