#pragma once

#include "model.h"

/// OpenDocument Presentation: what LibreOffice Impress saves and opens.
namespace Odp {

bool read(const QByteArray &bytes, Model::Presentation &deck, QString *error = nullptr);
/// The deck as an .odp, every colour and face written out; empty when it cannot be made.
QByteArray write(const Model::Presentation &deck);

}
