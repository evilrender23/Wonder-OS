"""Read the list of editions of a Windows image (install.wim / install.esd).

Only the uncompressed XML catalogue at the end of the file is read, so this
needs no WIM library.
"""

from __future__ import annotations

import struct
import xml.etree.ElementTree as ET
from typing import BinaryIO

MAGIC = b"MSWIM\x00\x00\x00"


class WimError(ValueError):
    pass


def editions(stream: BinaryIO) -> list[dict[str, str]]:
    """[{index, name, edition, build}] for every image in the WIM."""
    header = stream.read(208)
    if len(header) < 208 or header[:8] != MAGIC:
        raise WimError("not a WIM image")
    # The XML data resource entry: 7 bytes of size, 1 of flags, offset, original size.
    packed, offset, _original = struct.unpack_from("<QQQ", header, 0x48)
    size = packed & 0x00FFFFFFFFFFFFFF
    if not 0 < size < 64 << 20:
        raise WimError("WIM catalogue has an implausible size")
    stream.seek(offset)
    raw = stream.read(size)
    try:
        root = ET.fromstring(raw.decode("utf-16"))
    except (UnicodeError, ET.ParseError) as error:
        raise WimError(f"unreadable WIM catalogue: {error}") from error
    found = []
    for image in root.iter("IMAGE"):
        windows = image.find("WINDOWS")
        found.append(
            {
                "index": image.get("INDEX", ""),
                "name": (image.findtext("NAME") or "").strip(),
                "edition": (windows.findtext("EDITIONID") if windows is not None else "") or "",
                "build": (windows.findtext("VERSION/BUILD") if windows is not None else "") or "",
            }
        )
    return found


def pick(found: list[dict[str, str]]) -> dict[str, str]:
    """The edition to install: the Remote Desktop host needs Pro or better."""
    for wanted in ("Professional", "Enterprise", "ProfessionalWorkstation", "Education", "ProfessionalEducation"):
        for image in found:
            if image["edition"] == wanted:
                return image
    names = ", ".join(i["name"] for i in found) or "none"
    raise WimError(f"this image has no edition with a Remote Desktop host (found: {names})")
