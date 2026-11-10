"""Experimental: lend the host's graphics card to Windows without giving it away.

Windows gets a paravirtual card (virtio-gpu). What programs draw with it
travels as Vulkan commands (the «Venus» protocol) to the host, whose own driver
runs them on the real card, shared with everything else on the desktop. No
card is reserved, nothing is rebooted, and it works with a single card.

The Windows side needs a driver that is not finished upstream; see
guest/gpu.ps1. Until it is, this stays off unless the user turns it on.
"""

from __future__ import annotations

import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

DRM = Path("/sys/class/drm")
ICDS = (Path("/usr/share/vulkan/icd.d"), Path("/etc/vulkan/icd.d"))

# Kernel driver -> the Vulkan driver that goes with it. The proprietary NVIDIA
# driver is left out on purpose: stock QEMU cannot show what it renders (the
# guest's screen stays black), so that card is only used when asked by name.
VULKAN = {"amdgpu": "radeon", "i915": "intel", "xe": "intel", "nouveau": "nouveau"}


@dataclass(frozen=True)
class Card:
    node: Path  # /dev/dri/renderD*
    driver: str
    icd: Path | None


def cards() -> list[Card]:
    found = []
    for entry in sorted(DRM.glob("renderD*")):
        try:
            driver = (entry / "device" / "driver").resolve().name
        except OSError:
            continue
        node = Path("/dev/dri") / entry.name
        if os.access(node, os.R_OK | os.W_OK):
            found.append(Card(node, driver, _icd(driver)))
    return found


def _icd(driver: str) -> Path | None:
    name = VULKAN.get(driver)
    if not name:
        return None
    for folder in ICDS:
        for candidate in sorted(folder.glob(f"{name}_icd*.json")):
            return candidate
    return None


def choose(wanted: str = "") -> Card | None:
    """The card Windows will share: the one asked for, or the first that can."""
    available = cards()
    if wanted:
        for card in available:
            if wanted in (str(card.node), card.node.name, card.driver):
                return card
        return None
    for card in available:
        if card.icd is not None:
            return card
    return None


def qemu_can() -> bool:
    try:
        result = subprocess.run(
            ["qemu-system-x86_64", "-device", "virtio-vga-gl,help"],
            capture_output=True, text=True, check=False, timeout=20,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0 and "venus=" in result.stdout


def problem(wanted: str = "") -> str | None:
    """Why the card cannot be shared, or None when it can."""
    if not qemu_can():
        return "this QEMU has no virtio-gpu with Venus (packages «qemu-hw-display-virtio-vga-gl», «virglrenderer»)"
    if choose(wanted) is None:
        if wanted:
            return f"there is no graphics card «{wanted}» to share"
        return "no graphics card with a Mesa Vulkan driver was found (AMD or Intel; NVIDIA's own driver is not supported yet)"
    return None


def arguments(memory_mb: int, video_mb: int, wanted: str = "") -> tuple[list[str], dict[str, str]]:
    """What to add to the machine, and to its environment."""
    card = choose(wanted)
    if card is None:
        raise RuntimeError(problem(wanted) or "no graphics card to share")
    environment = {}
    if card.icd is not None:
        # Only this card's Vulkan driver: Windows must not see the others.
        environment["VK_DRIVER_FILES"] = str(card.icd)
        environment["VK_ICD_FILENAMES"] = str(card.icd)
    return [
        # Venus maps the card's memory into the guest: its memory must be shareable.
        "-object", f"memory-backend-memfd,id=ram,size={memory_mb}M,share=on",
        "-machine", "memory-backend=ram",
        "-vga", "none",
        "-device", f"virtio-vga-gl,blob=on,venus=on,hostmem={video_mb}M",
        "-display", f"egl-headless,rendernode={card.node}",
    ], environment
