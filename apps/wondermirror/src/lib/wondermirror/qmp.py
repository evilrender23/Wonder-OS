"""A minimal client for QEMU's machine protocol (QMP)."""

from __future__ import annotations

import json
import socket
from pathlib import Path


class QmpError(RuntimeError):
    pass


class Qmp:
    def __init__(self, path: Path, timeout: float = 10.0) -> None:
        self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._socket.settimeout(timeout)
        try:
            self._socket.connect(str(path))
        except OSError as error:
            self._socket.close()
            raise QmpError(f"cannot reach the machine: {error}") from error
        self._file = self._socket.makefile("rwb")
        self._read()  # greeting
        self.command("qmp_capabilities")

    def _read(self) -> dict:
        try:
            line = self._file.readline()
        except OSError as error:
            raise QmpError(f"the machine stopped answering: {error}") from error
        if not line:
            raise QmpError("the machine closed the connection")
        return json.loads(line)

    def command(self, name: str, **arguments: object) -> object:
        message: dict[str, object] = {"execute": name}
        if arguments:
            message["arguments"] = arguments
        try:
            self._file.write(json.dumps(message).encode() + b"\n")
            self._file.flush()
        except OSError as error:
            raise QmpError(f"the machine stopped answering: {error}") from error
        while True:
            answer = self._read()
            if "event" in answer:
                continue
            if "error" in answer:
                raise QmpError(str(answer["error"].get("desc", answer["error"])))
            return answer.get("return")

    def close(self) -> None:
        try:
            self._file.close()
        finally:
            self._socket.close()

    def __enter__(self) -> "Qmp":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
