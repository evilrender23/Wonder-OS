"""The answer file that installs Windows without a single question.

Windows Setup reads `autounattend.xml` from the root of any removable disc, so
it travels on a tiny second CD next to the Windows image.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path
from xml.sax.saxutils import escape

from .config import Account

# Microsoft's public generic key for Windows 11 Pro: it selects the edition
# during setup and does not activate it.
GENERIC_PRO_KEY = "VK7JG-NPHTM-C97JM-9MPGT-3V66T"
IMAGE_NAME = "Windows 11 Pro"
COMPUTER_NAME = "WONDERMIRROR"
VOLUME_LABEL = "WMIRROR"

_ARCH = 'processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS"'

# The machine has a virtual TPM, but the firmware runs without Secure Boot keys.
_CHECKS = ("BypassTPMCheck", "BypassSecureBootCheck", "BypassRAMCheck", "BypassCPUCheck", "BypassStorageCheck")


def _commands(tag: str, commands: list[str], line: str = "Path") -> str:
    out = []
    for order, command in enumerate(commands, start=1):
        out.append(
            f'<{tag} wcm:action="add"><Order>{order}</Order><{line}>{escape(command)}</{line}></{tag}>'
        )
    return "".join(out)


def _international(locale: str) -> str:
    value = escape(locale)
    return (
        f"<InputLocale>{value}</InputLocale><SystemLocale>{value}</SystemLocale>"
        f"<UILanguage>{value}</UILanguage><UserLocale>{value}</UserLocale>"
    )


def answer_file(account: Account, locale: str, image_name: str = IMAGE_NAME) -> str:
    user, password = escape(account.user), escape(account.password)
    bypass = [f"reg add HKLM\\SYSTEM\\Setup\\LabConfig /v {name} /t REG_DWORD /d 1 /f" for name in _CHECKS]
    first_logon = (
        'cmd /c "for %d in (D E F G H I J) do @if exist %d:\\wondermirror\\setup.ps1 '
        'powershell -NoProfile -ExecutionPolicy Bypass -File %d:\\wondermirror\\setup.ps1"'
    )
    return f"""<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
  <settings pass="windowsPE">
    <component name="Microsoft-Windows-International-Core-WinPE" {_ARCH}>
      <SetupUILanguage><UILanguage>{escape(locale)}</UILanguage></SetupUILanguage>
      {_international(locale)}
    </component>
    <component name="Microsoft-Windows-Setup" {_ARCH}>
      <RunSynchronous>{_commands("RunSynchronousCommand", bypass)}</RunSynchronous>
      <DiskConfiguration>
        <WillShowUI>OnError</WillShowUI>
        <Disk wcm:action="add">
          <DiskID>0</DiskID>
          <WillWipeDisk>true</WillWipeDisk>
          <CreatePartitions>
            <CreatePartition wcm:action="add"><Order>1</Order><Type>EFI</Type><Size>300</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>2</Order><Type>MSR</Type><Size>16</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>3</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>
          </CreatePartitions>
          <ModifyPartitions>
            <ModifyPartition wcm:action="add"><Order>1</Order><PartitionID>1</PartitionID><Format>FAT32</Format><Label>System</Label></ModifyPartition>
            <ModifyPartition wcm:action="add"><Order>2</Order><PartitionID>2</PartitionID></ModifyPartition>
            <ModifyPartition wcm:action="add"><Order>3</Order><PartitionID>3</PartitionID><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>
          </ModifyPartitions>
        </Disk>
      </DiskConfiguration>
      <ImageInstall>
        <OSImage>
          <InstallFrom><MetaData wcm:action="add"><Key>/IMAGE/NAME</Key><Value>{escape(image_name)}</Value></MetaData></InstallFrom>
          <InstallTo><DiskID>0</DiskID><PartitionID>3</PartitionID></InstallTo>
          <WillShowUI>OnError</WillShowUI>
        </OSImage>
      </ImageInstall>
      <UserData>
        <ProductKey><Key>{GENERIC_PRO_KEY}</Key><WillShowUI>OnError</WillShowUI></ProductKey>
        <AcceptEula>true</AcceptEula>
      </UserData>
    </component>
  </settings>
  <settings pass="specialize">
    <component name="Microsoft-Windows-Shell-Setup" {_ARCH}>
      <ComputerName>{COMPUTER_NAME}</ComputerName>
    </component>
    <component name="Microsoft-Windows-Deployment" {_ARCH}>
      <RunSynchronous>{_commands("RunSynchronousCommand", [
          "reg add HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OOBE /v BypassNRO /t REG_DWORD /d 1 /f",
      ])}</RunSynchronous>
    </component>
  </settings>
  <settings pass="oobeSystem">
    <component name="Microsoft-Windows-International-Core" {_ARCH}>
      {_international(locale)}
    </component>
    <component name="Microsoft-Windows-Shell-Setup" {_ARCH}>
      <OOBE>
        <HideEULAPage>true</HideEULAPage>
        <HideOEMRegistrationScreen>true</HideOEMRegistrationScreen>
        <HideOnlineAccountScreens>true</HideOnlineAccountScreens>
        <HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE>
        <HideLocalAccountScreen>true</HideLocalAccountScreen>
        <ProtectYourPC>3</ProtectYourPC>
      </OOBE>
      <UserAccounts>
        <LocalAccounts>
          <LocalAccount wcm:action="add">
            <Name>{user}</Name>
            <Group>Administrators</Group>
            <Password><Value>{password}</Value><PlainText>true</PlainText></Password>
          </LocalAccount>
        </LocalAccounts>
      </UserAccounts>
      <AutoLogon>
        <Enabled>true</Enabled>
        <Username>{user}</Username>
        <Password><Value>{password}</Value><PlainText>true</PlainText></Password>
        <LogonCount>1</LogonCount>
      </AutoLogon>
      <FirstLogonCommands>{_commands("SynchronousCommand", [first_logon], line="CommandLine")}</FirstLogonCommands>
    </component>
  </settings>
</unattend>
"""


class UnattendError(RuntimeError):
    pass


def build_disc(target: Path, account: Account, locale: str, guest_dir: Path) -> Path:
    """Write the answer disc: autounattend.xml plus the first-logon script."""
    tool = shutil.which("xorriso")
    if not tool:
        raise UnattendError("xorriso is missing (package «libisoburn»)")
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent) as work:
        root = Path(work)
        root.chmod(0o700)
        # Windows Setup wants a byte order mark and CRLF is the safe choice.
        (root / "autounattend.xml").write_bytes(
            b"\xef\xbb\xbf" + answer_file(account, locale).replace("\n", "\r\n").encode("utf-8")
        )
        (root / "wondermirror").mkdir()
        shutil.copy(guest_dir / "setup.ps1", root / "wondermirror" / "setup.ps1")
        temporary = target.with_name(target.name + ".tmp")
        result = subprocess.run(
            [tool, "-as", "mkisofs", "-quiet", "-J", "-r", "-V", VOLUME_LABEL, "-o", str(temporary), str(root)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            temporary.unlink(missing_ok=True)
            raise UnattendError(f"xorriso failed: {result.stderr.strip()[-400:]}")
        # The disc carries the account's password in clear text.
        temporary.chmod(0o600)
        temporary.replace(target)
    return target
