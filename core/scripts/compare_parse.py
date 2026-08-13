"""Compare Go parse vs Python LiveProtobuf on sample frames (if any)."""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE_EXE = ROOT / "core" / "dist" / "liveaio-core.exe"
TESTDATA = ROOT / "core" / "testdata"


def main() -> int:
    TESTDATA.mkdir(parents=True, exist_ok=True)
    samples = sorted(TESTDATA.glob("*.bin"))
    if not samples:
        # Self-check: empty PushFrame is not ok — report harness ready.
        print("No *.bin in core/testdata yet — harness OK. Drop frames to enable A/B.")
        print("Python try_parse_frame import:", end=" ")
        from listener.LiveProtobuf import try_parse_frame
        ok, msgs = try_parse_frame(b"")
        print(f"empty→ ok={ok} n={len(msgs)}")
        return 0

    from listener.LiveProtobuf import try_parse_frame
    import base64
    import socket
    import time

    if not CORE_EXE.is_file():
        print("missing", CORE_EXE)
        return 1

    # Start core briefly and push each frame
    proc = subprocess.Popen(
        [str(CORE_EXE), "--tcp", "127.0.0.1:19877", "--root", str(ROOT)],
        cwd=str(ROOT),
    )
    time.sleep(0.4)
    try:
        s = socket.create_connection(("127.0.0.1", 19877), timeout=2)
        # drain ready
        s.settimeout(1.0)
        _ = s.recv(4096)
        for path in samples:
            raw = path.read_bytes()
            py_ok, py_msgs = try_parse_frame(raw)
            payload = json.dumps({
                "op": "frame.push",
                "payload_b64": base64.b64encode(raw).decode("ascii"),
            }) + "\n"
            s.sendall(payload.encode())
            time.sleep(0.05)
            buf = b""
            try:
                while True:
                    chunk = s.recv(65536)
                    if not chunk:
                        break
                    buf += chunk
                    if b"\n" in buf:
                        break
            except socket.timeout:
                pass
            go_types = []
            for line in buf.split(b"\n"):
                if not line.strip():
                    continue
                env = json.loads(line.decode())
                if env.get("op") == "message":
                    go_types.append(env.get("type"))
            py_types = [getattr(m, "type", None) for m in py_msgs]
            print(path.name, "py_ok", py_ok, "py", py_types, "go", go_types)
            if py_types != go_types:
                print(" MISMATCH")
                return 2
        print("all matched")
        return 0
    finally:
        try:
            s.sendall(b'{"op":"shutdown"}\n')
            s.close()
        except Exception:
            pass
        proc.terminate()
        proc.wait(timeout=3)


if __name__ == "__main__":
    sys.exit(main())
