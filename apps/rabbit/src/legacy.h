#pragma once

#include <QString>

/// Rabbit was called Finder, and kept its settings and each folder's layout
/// under that name.
namespace Legacy
{
/// Brings what Finder left in `configRoot` (~/.config) and `dataRoot`
/// (~/.local/share) over to Rabbit's names. What Rabbit already has stays.
void adopt(const QString &configRoot, const QString &dataRoot);
}
