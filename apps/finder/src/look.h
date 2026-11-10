#pragma once

#include <QString>

/// Quick Look: what a file is shown as when Space is pressed on it.
namespace Look
{
/// "folder", "image", "animation", "text", "pdf", "audio", "video", or
/// "other" for what is shown as its thumbnail or icon.
QString kindFor(const QString &mime, bool isDir);
}
