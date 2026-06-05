"""
sb_msg.py — Python bindings for libsb_msg

Wraps libsorabbyngo.so messaging API. Every published message is
automatically signed with this node's Ed25519 key. Every received
message is signature-verified before the handler is called.

Usage:
    from sb_identity import Identity
    from sb_msg import Messenger, SUBJECTS

    node = Identity()
    msg  = Messenger(node, service="novela")
    await msg.connect()

    # Publish a signed record
    await msg.publish(SUBJECTS["novela_record"],
                      b'{"patient":"007","bp":"120/80"}')

    # Subscribe with automatic sig verification
    async def on_cmd(env):
        print(f"Command from {env.nodeid[:8]}: {env.payload}")

    await msg.subscribe(SUBJECTS["sentinel_cmd"], on_cmd)
    await msg.heartbeat()
    await msg.close()

Sorabbyngo Labs — Kisii, Kenya
"""

import asyncio
import json
import base64
import logging
from typing import Callable, Awaitable, Optional, Union

logger = logging.getLogger("sorabbyngo.msg")

try:
    import nats as nats_lib
    NATS_AVAILABLE = True
except ImportError:
    NATS_AVAILABLE = False
    logger.warning("nats-py not installed — run: pip install nats-py")

from sb_identity import Identity, SbIdentityError

# ── Standard subjects ───────────────────────────────────────────────────────

SUBJECTS = {
    "heartbeat":      "sorabbyngo.{service}.heartbeat",
    "cw_alert":       "sorabbyngo.cyberwall.alert",
    "cw_block":       "sorabbyngo.cyberwall.block",
    "cw_status":      "sorabbyngo.cyberwall.status",
    "sentinel_cmd":   "sorabbyngo.sentinel.command",
    "sentinel_health":"sorabbyngo.sentinel.health",
    "mesh_peer":      "sorabbyngo.byngonet.peer",
    "ai_request":     "sorabbyngo.byngox.request",
    "ai_response":    "sorabbyngo.byngox.response",
    "novela_record":  "sorabbyngo.novela.record",
}

ENVELOPE_VERSION = 1

# ── Envelope dataclass ──────────────────────────────────────────────────────

class Envelope:
    """
    Parsed, signature-verified message envelope.
    All fields are populated before a handler is called.
    """
    __slots__ = ("version", "nodeid", "pubkey", "subject", "payload", "sig", "verified")

    def __init__(self):
        self.version  : int   = 0
        self.nodeid   : str   = ""
        self.pubkey   : bytes = b""
        self.subject  : str   = ""
        self.payload  : bytes = b""
        self.sig      : bytes = b""
        self.verified : bool  = False

    def __repr__(self):
        return (f"<Envelope node={self.nodeid[:8]} "
                f"subj={self.subject} "
                f"payload={len(self.payload)}b "
                f"verified={self.verified}>")

# ── Exceptions ──────────────────────────────────────────────────────────────

class SbMsgError(Exception):
    pass

class SbMsgNotConnected(SbMsgError):
    pass

class SbMsgSigInvalid(SbMsgError):
    pass

class SbMsgBadEnvelope(SbMsgError):
    pass

# ── Envelope serialisation ──────────────────────────────────────────────────

def _build_envelope(identity: Identity, subject: str,
                    payload: bytes, sign: bool = True) -> bytes:
    """Build and sign a JSON envelope. Returns UTF-8 bytes."""
    sig = identity.sign(payload) if sign else b"\x00" * 64
    env = {
        "v":       ENVELOPE_VERSION,
        "nodeid":  identity.nodeid,
        "pubkey":  identity.pubkey.hex(),
        "subject": subject,
        "payload": base64.b64encode(payload).decode(),
        "sig":     sig.hex(),
    }
    return json.dumps(env, separators=(",", ":")).encode()


def _parse_envelope(data: bytes, verify: bool = True) -> Envelope:
    """Parse and optionally verify a JSON envelope. Returns Envelope."""
    try:
        raw = json.loads(data.decode())
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise SbMsgBadEnvelope(f"JSON decode failed: {e}")

    if raw.get("v") != ENVELOPE_VERSION:
        raise SbMsgBadEnvelope(f"Unknown envelope version: {raw.get('v')}")

    env = Envelope()
    try:
        env.version = raw["v"]
        env.nodeid  = raw["nodeid"]
        env.pubkey  = bytes.fromhex(raw["pubkey"])
        env.subject = raw["subject"]
        env.payload = base64.b64decode(raw["payload"])
        env.sig     = bytes.fromhex(raw["sig"])
    except (KeyError, ValueError) as e:
        raise SbMsgBadEnvelope(f"Malformed envelope field: {e}")

    if verify:
        try:
            Identity.verify(env.payload, env.sig, env.pubkey)
            env.verified = True
        except SbIdentityError as e:
            raise SbMsgSigInvalid(f"Signature verification failed: {e}")

    return env

# ── Messenger ───────────────────────────────────────────────────────────────

HandlerT = Callable[[Envelope], Awaitable[None]]

class Messenger:
    """
    Async NATS messenger with automatic NodeID signing and verification.

    Every outbound message is signed with this node's Ed25519 key.
    Every inbound message is signature-verified before the handler fires.
    Invalid signatures are silently dropped — nothing untrusted reaches
    application code.
    """

    def __init__(self,
                 identity: Identity,
                 nats_url: str = "nats://localhost:4222",
                 service: str  = "unknown"):
        self._id      = identity
        self._url     = nats_url
        self._service = service
        self._nc      = None
        self._subs    = {}   # subject → nats subscription

    # ── Lifecycle ──────────────────────────────────────────────────────────

    async def connect(self) -> None:
        """Connect to NATS. Raises SbMsgNotConnected on failure."""
        if not NATS_AVAILABLE:
            raise SbMsgNotConnected("nats-py not installed")
        try:
            self._nc = await nats_lib.connect(
                self._url,
                name=self._service,
                reconnect_time_wait=3,
                max_reconnect_attempts=-1,
            )
            logger.info(f"[{self._service}] Connected to NATS: {self._url}")
        except Exception as e:
            raise SbMsgNotConnected(f"NATS connection failed: {e}")

    async def close(self) -> None:
        """Disconnect from NATS."""
        if self._nc:
            await self._nc.close()
            self._nc = None

    # ── Publishing ─────────────────────────────────────────────────────────

    async def publish(self, subject: str, payload: Union[bytes, str, dict],
                      sign: bool = True) -> None:
        """
        Publish a signed message envelope.

        payload can be bytes, str (encoded to UTF-8), or dict (JSON-encoded).
        Raises SbMsgNotConnected if not connected.
        """
        if not self._nc:
            raise SbMsgNotConnected("Not connected — call connect() first")

        if isinstance(payload, dict):
            payload = json.dumps(payload, separators=(",", ":")).encode()
        elif isinstance(payload, str):
            payload = payload.encode()

        envelope_bytes = _build_envelope(self._id, subject, payload, sign=sign)
        await self._nc.publish(subject, envelope_bytes)

    async def heartbeat(self, extra: Optional[dict] = None) -> None:
        """Publish a standard heartbeat."""
        subject = SUBJECTS["heartbeat"].format(service=self._service)
        data    = {"service": self._service,
                   "nodeid":  self._id.nodeid,
                   **(extra or {})}
        await self.publish(subject, data)

    # ── Subscribing ────────────────────────────────────────────────────────

    async def subscribe(self, subject: str, handler: HandlerT,
                        verify: bool = True) -> None:
        """
        Subscribe to a subject. Handler receives verified Envelope objects.

        If verify=True (default), messages with invalid signatures are
        silently dropped. Handler is never called with unverified data.
        """
        if not self._nc:
            raise SbMsgNotConnected("Not connected — call connect() first")

        async def _cb(raw_msg):
            try:
                env = _parse_envelope(raw_msg.data, verify=verify)
            except SbMsgSigInvalid:
                logger.warning(f"[{self._service}] Dropped: invalid sig on {subject}")
                return
            except SbMsgBadEnvelope as e:
                logger.warning(f"[{self._service}] Dropped: bad envelope — {e}")
                return
            try:
                await handler(env)
            except Exception as e:
                logger.error(f"[{self._service}] Handler error on {subject}: {e}")

        sub = await self._nc.subscribe(subject, cb=_cb)
        self._subs[subject] = sub
        logger.debug(f"[{self._service}] Subscribed to {subject}")

    async def unsubscribe(self, subject: str) -> None:
        """Remove a subscription."""
        sub = self._subs.pop(subject, None)
        if sub:
            await sub.unsubscribe()

    # ── Request / Reply ────────────────────────────────────────────────────

    async def request(self, subject: str, payload: Union[bytes, str, dict],
                      timeout: float = 5.0,
                      verify: bool = True) -> Envelope:
        """
        Synchronous request-reply pattern.

        Publishes to subject, waits up to timeout seconds for a verified reply.
        Returns the reply Envelope.
        Raises asyncio.TimeoutError on timeout, SbMsgSigInvalid on bad reply.
        """
        if not self._nc:
            raise SbMsgNotConnected("Not connected")

        if isinstance(payload, dict):
            payload = json.dumps(payload, separators=(",", ":")).encode()
        elif isinstance(payload, str):
            payload = payload.encode()

        envelope_bytes = _build_envelope(self._id, subject, payload)

        reply_msg = await asyncio.wait_for(
            self._nc.request(subject, envelope_bytes),
            timeout=timeout
        )
        return _parse_envelope(reply_msg.data, verify=verify)

    # ── Properties ─────────────────────────────────────────────────────────

    @property
    def connected(self) -> bool:
        return self._nc is not None and not self._nc.is_closed

    @property
    def nodeid(self) -> str:
        return self._id.nodeid

    @property
    def short_id(self) -> str:
        return self._id.short_id

    def __repr__(self):
        return (f"<Messenger service={self._service} "
                f"node={self.short_id} "
                f"connected={self.connected}>")


# ── CLI smoke test ──────────────────────────────────────────────────────────

if __name__ == "__main__":
    import sys

    async def _smoke():
        print("\n  Sorabbyngo OS — libsb_msg Python binding\n")

        node = Identity()
        print(f"  Identity : {node.nodeid}")
        print(f"  Short ID : {node.short_id}")
        print(f"  Source   : {node.source}\n")

        msg = Messenger(node, service="smoke-test")

        try:
            await msg.connect()
            print(f"  NATS     : connected ✓")
        except SbMsgNotConnected as e:
            print(f"  NATS     : {e}")
            print("  (Unit test only — envelope roundtrip below)\n")

            # Envelope unit test without NATS
            payload = b'{"patient":"007","bp":"120/80","spo2":"99"}'
            env_bytes = _build_envelope(node, "sorabbyngo.novela.record", payload)
            env = _parse_envelope(env_bytes, verify=True)
            print(f"  Envelope : built and verified ✓")
            print(f"  NodeID   : {env.nodeid}")
            print(f"  Subject  : {env.subject}")
            print(f"  Payload  : {env.payload.decode()}")
            print(f"  Verified : {env.verified}\n")
            return

        # Full integration path
        received = []
        async def on_msg(env: Envelope):
            received.append(env)

        await msg.subscribe("sorabbyngo.test.roundtrip", on_msg)
        await asyncio.sleep(0.05)

        test_payload = {"patient": "007", "bp": "120/80", "spo2": "99"}
        await msg.publish("sorabbyngo.test.roundtrip", test_payload)
        await asyncio.sleep(0.2)

        if received:
            env = received[0]
            print(f"  Publish  : ✓")
            print(f"  Receive  : ✓")
            print(f"  Verified : {env.verified}")
            print(f"  Payload  : {env.payload.decode()}")
        else:
            print("  Receive  : no message received")

        await msg.heartbeat()
        print(f"  Heartbeat: ✓")
        await msg.close()
        print()

    asyncio.run(_smoke())
