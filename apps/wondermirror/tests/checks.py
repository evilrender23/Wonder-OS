"""Checks of Wonder Mirror that need no Windows machine. Run by tests/run.sh."""

import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root / "src" / "lib"))

from wondermirror import config, gpu, keyboard  # noqa: E402

failures = 0


def check(name: str, passed: bool) -> None:
    global failures
    print(("ok   " if passed else "FAIL ") + name)
    failures += not passed


def cli(*arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(root / "src" / "wondermirror"), *arguments],
                          capture_output=True, text=True, timeout=60)


# Settings
settings = config.load_settings()
check("memory defaults to 4–16 GiB", 4096 <= settings.effective_memory_mb <= 16384)
check("the shared graphics card is off by default", settings.gpu is False)
check("the keyboard follows this computer by default", settings.keyboard == keyboard.FOLLOW)

# Keyboard layouts
klids = [layout.klid for layout in keyboard.LAYOUTS]
check("every layout has a Windows KLID of 8 hex digits",
      all(len(k) == 8 and int(k, 16) >= 0 for k in klids))
check("no layout is listed twice", len(set(klids)) == len(klids))
check("es is Spanish", keyboard.find("es").klid.upper() == "0000040A")
check("us is English (US)", keyboard.find("us").klid == "00000409")
check("a raw KLID is accepted", keyboard.find("0000080A") is not None)
check("an unknown layout is refused", keyboard.find("klingon") is None)
check("«auto» is a valid setting, nonsense is not",
      keyboard.valid(keyboard.FOLLOW) and not keyboard.valid("klingon"))

# Shared graphics card: only when a card can be shared on this computer
if gpu.problem("") is None:
    arguments, _ = gpu.arguments(8192, 2048)
    joined = " ".join(arguments)
    check("the machine's memory is shareable and of the asked size",
          "memory-backend-memfd,id=ram,size=8192M,share=on" in joined)
    check("the card gets Venus and the asked memory", "venus=on,hostmem=2048M" in joined)
else:
    print("skip shared graphics card: " + gpu.problem(""))
check("a card that does not exist is a problem", gpu.problem("no-such-card") is not None)

# Command line, without a machine
check("--help works", cli("--help").returncode == 0)
status = cli("status")
check("status without a machine does not crash", "Traceback" not in status.stderr)
shown = cli("config", "memory_mb")
check("config shows a setting", shown.returncode == 0 and shown.stdout.strip() == "0")
check("config stores a setting",
      cli("config", "memory_mb", "8192").returncode == 0 and cli("config", "memory_mb").stdout.strip() == "8192")
check("config refuses too little memory", cli("config", "memory_mb", "64").returncode != 0)
check("config refuses an unknown setting", cli("config", "nonsense", "1").returncode != 0)
check("keyboard list names Spanish", "Spanish" in cli("keyboard", "list").stdout)
check("keyboard refuses an unknown layout", cli("keyboard", "klingon").returncode != 0)

# Guest scripts and patches are in place
for name in ("agent.ps1", "setup.ps1", "list-apps.ps1", "keyboard.ps1", "gpu.ps1"):
    check(f"guest/{name} exists", (root / "guest" / name).is_file())
patch = (root / "patches" / "freerdp-remoteapp-menus.patch").read_text()
check("the FreeRDP patch touches xf_rail.c", "client/X11/xf_rail.c" in patch)

sys.exit(1 if failures else 0)
