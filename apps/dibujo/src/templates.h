#pragma once

#include "model.h"

#include <QVariantList>

/// The drawings a new one can start from.
namespace Templates
{
/// {key, name, detail}.
QVariantList list();
/// blank, flow, mindmap, wireframe, poster — or sample, a bit of everything.
Document make(const QString &key);
}
