#include "preview.h"

#include "model.h"
#include "odp.h"
#include "pptx.h"
#include "render.h"

#include <QFile>
#include <QUrl>

using namespace Model;

PreviewProvider::PreviewProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage PreviewProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QStringList parts = QUrl::fromPercentEncoding(id.toUtf8()).split(QLatin1Char('/'));
    Presentation deck;
    int index = 0;
    if (parts.value(0) == QLatin1String("template")) {
        deck.theme = theme(parts.value(1)).id;
        Slide slide = newSlide(QStringLiteral("title"), deck.size());
        for (Item &item : slide.items) {
            item.setPlainText(item.role == QLatin1String("ctitle") ? theme(deck.theme).name : QStringLiteral("Una presentación con este tema"));
        }
        deck.slides << slide;
    } else if (parts.value(0) == QLatin1String("file")) {
        const QString path = QString::fromUtf8(QByteArray::fromHex(parts.value(1).toLatin1()));
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            if (path.endsWith(QLatin1String(".odp"), Qt::CaseInsensitive)) {
                Odp::read(bytes, deck);
            } else {
                Pptx::read(bytes, deck);
            }
        }
        index = qBound(0, parts.value(2).toInt(), qMax(0, int(deck.slides.size()) - 1));
    }
    if (deck.slides.isEmpty()) {
        deck.slides << newSlide(QStringLiteral("blank"), deck.size());
    }
    const double width = requestedSize.width() > 0 ? requestedSize.width() : 480;
    const QImage image = Render::image(deck, index, width / deck.width);
    if (size) {
        *size = image.size();
    }
    return image;
}
