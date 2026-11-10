#pragma once

#include <QDomDocument>
#include <QList>
#include <QString>

/// What the readers need of QDom: children by local name, whatever the prefix.
namespace Xml {

inline QDomDocument parse(const QByteArray &bytes, bool *ok = nullptr)
{
    QDomDocument document;
    const auto result = document.setContent(bytes, QDomDocument::ParseOption::UseNamespaceProcessing);
    if (ok) {
        *ok = bool(result);
    }
    return document;
}

inline QList<QDomElement> kids(const QDomElement &parent, const char *name = nullptr)
{
    QList<QDomElement> out;
    for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        if (!name || e.localName() == QLatin1String(name)) {
            out << e;
        }
    }
    return out;
}

inline QDomElement kid(const QDomElement &parent, const char *name)
{
    for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        if (e.localName() == QLatin1String(name)) {
            return e;
        }
    }
    return {};
}

inline QDomElement kid(const QDomElement &parent, std::initializer_list<const char *> path)
{
    QDomElement e = parent;
    for (const char *name : path) {
        e = kid(e, name);
        if (e.isNull()) {
            break;
        }
    }
    return e;
}

/// Every descendant with this local name, in document order.
inline void all(const QDomElement &parent, const char *name, QList<QDomElement> &out)
{
    for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        if (e.localName() == QLatin1String(name)) {
            out << e;
        }
        all(e, name, out);
    }
}

/// An attribute by its local name: `r:embed`, `draw:name`, whatever the prefix is bound to.
inline QString attr(const QDomElement &e, const char *name, const QString &fallback = QString())
{
    const QDomNamedNodeMap map = e.attributes();
    for (int i = 0; i < map.count(); ++i) {
        const QDomAttr a = map.item(i).toAttr();
        const QString local = a.localName().isEmpty() ? a.name() : a.localName();
        if (local == QLatin1String(name)) {
            return a.value();
        }
    }
    return fallback;
}

}
