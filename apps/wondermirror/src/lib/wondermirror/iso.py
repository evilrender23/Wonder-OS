"""Download the official Windows 11 ISO from Microsoft.

Microsoft serves the consumer ISO through a short-lived link that its download
page builds with a handful of requests. This follows the same sequence (the one
documented by the Fido and Mido projects), so the image always comes straight
from Microsoft's servers.
"""

from __future__ import annotations

import json
import os
import re
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from typing import Callable

PAGE = "https://www.microsoft.com/en-us/software-download/windows11"
CONNECTOR = "https://www.microsoft.com/software-download-connector/api"
PROFILE = "606624d44113"
USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0"

# Locale -> the language name Microsoft uses in its SKU table.
LANGUAGES = {
    "es-ES": "Spanish",
    "es-MX": "Spanish (Mexico)",
    "en-US": "English (United States)",
    "en-GB": "English International",
    "fr-FR": "French",
    "de-DE": "German",
    "it-IT": "Italian",
    "pt-BR": "Brazilian Portuguese",
    "pt-PT": "Portuguese",
}


class IsoError(RuntimeError):
    pass


def _get(url: str, referer: str | None = None, limit: int = 2_000_000) -> bytes:
    headers = {"User-Agent": USER_AGENT}
    if referer:
        headers["Referer"] = referer
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.read(limit)
    except (urllib.error.URLError, TimeoutError) as error:
        raise IsoError(f"{url.split('?')[0]}: {error}") from error


def product_edition_id(html: str) -> str:
    """The id of the newest multi-edition release listed on the download page."""
    match = re.search(r'<option value="(\d{1,16})">Windows', html)
    if not match:
        raise IsoError("the download page lists no Windows release")
    return match.group(1)


def sku_for(table: dict, language: str) -> str:
    for sku in table.get("Skus", []):
        if language in (sku.get("Language"), sku.get("LocalizedLanguage")):
            return str(sku["Id"])
    raise IsoError(f"Microsoft offers no image in «{language}»")


def link_from(answer: dict) -> str:
    """The x64 link of a GetProductDownloadLinksBySku answer."""
    errors = answer.get("Errors") or []
    if errors:
        message = "; ".join(str(e.get("Value") or e.get("Key")) for e in errors)
        raise IsoError(f"Microsoft rejected the request: {message}")
    for option in answer.get("ProductDownloadOptions") or []:
        uri = option.get("Uri", "")
        if "x64" in uri.split("?")[0]:
            return uri
    raise IsoError("Microsoft returned no x64 download link")


def resolve(locale: str = "en-US") -> tuple[str, str]:
    """Ask Microsoft for a download link. Returns (url, file name)."""
    language = LANGUAGES.get(locale, locale)
    session = str(uuid.uuid4())
    edition = product_edition_id(_get(PAGE).decode("utf-8", "replace"))
    _get(f"https://vlscppe.microsoft.com/tags?org_id=y6jn8c31&session_id={session}")
    common = f"profile={PROFILE}&friendlyFileName=undefined&Locale=en-US&sessionID={session}"
    table = json.loads(
        _get(f"{CONNECTOR}/getskuinformationbyproductedition?{common}&ProductEditionId={edition}&SKU=undefined")
    )
    sku = sku_for(table, language)
    answer = json.loads(
        _get(f"{CONNECTOR}/GetProductDownloadLinksBySku?{common}&productEditionId=undefined&SKU={sku}", referer=PAGE)
    )
    url = link_from(answer)
    name = os.path.basename(urllib.parse.urlparse(url).path) or "Win11_x64.iso"
    return url, name


def download(url: str, target: Path, progress: Callable[[int, int], None] | None = None) -> Path:
    """Download to `target`, resuming a partial `<target>.part` when there is one."""
    target.parent.mkdir(parents=True, exist_ok=True)
    part = target.with_name(target.name + ".part")
    done = part.stat().st_size if part.exists() else 0
    headers = {"User-Agent": USER_AGENT}
    if done:
        headers["Range"] = f"bytes={done}-"
    try:
        response = urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60)
    except urllib.error.HTTPError as error:
        if error.code != 416:
            raise IsoError(f"download failed: {error}") from error
        part.rename(target)
        return target
    except (urllib.error.URLError, TimeoutError) as error:
        raise IsoError(f"download failed: {error}") from error
    with response:
        if done and response.status != 206:
            done = 0
        total = done + int(response.headers.get("Content-Length") or 0)
        with open(part, "ab" if done else "wb") as out:
            while chunk := response.read(1 << 20):
                out.write(chunk)
                done += len(chunk)
                if progress:
                    progress(done, total)
    if total and done < total:
        raise IsoError(f"download interrupted at {done} of {total} bytes; run it again to resume")
    part.rename(target)
    return target
