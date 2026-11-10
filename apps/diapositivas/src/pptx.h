#pragma once

#include "model.h"

/// PresentationML: the .pptx that PowerPoint, Keynote and LibreOffice share.
namespace Pptx {

/// Reads a .pptx; false, with `error` saying why, when it is not one.
bool read(const QByteArray &bytes, Model::Presentation &deck, QString *error = nullptr);
QByteArray write(const Model::Presentation &deck);

}
