#include "templates.h"

#include "geometry.h"
#include "render.h"

#include <QUuid>

namespace
{
/// Puts objects on a drawing with few words.
struct Builder {
    Document doc;

    QString add(Shape s)
    {
        s.id = doc.freshId();
        if (s.name.isEmpty()) {
            s.name = doc.freshName(Geometry::baseName(s));
        }
        doc.shapes << s;
        return s.id;
    }

    QString frame(const QString &name, double x, double y, double w, double h, const QString &fill = QStringLiteral("#ffffff"))
    {
        Shape s;
        s.kind = QStringLiteral("frame");
        s.name = name;
        s.x = x;
        s.y = y;
        s.w = w;
        s.h = h;
        s.style.fill = QColor(fill);
        s.style.strokeWidth = 0;
        return add(s);
    }

    QString shape(const QString &form, double x, double y, double w, double h, const QString &text, const QString &fill, const QString &stroke = QStringLiteral("#1d1d1f"),
                  double radius = 0)
    {
        Shape s;
        s.kind = QStringLiteral("shape");
        s.form = form;
        s.x = x;
        s.y = y;
        s.w = w;
        s.h = h;
        s.text = text;
        s.style.fill = QColor(fill);
        s.style.radius = radius;
        if (stroke.isEmpty()) {
            s.style.strokeWidth = 0;
        } else {
            s.style.stroke = QColor(stroke);
        }
        return add(s);
    }

    QString text(double x, double y, const QString &words, double size, bool bold = false, const QString &color = QStringLiteral("#1d1d1f"), double width = 0)
    {
        Shape s;
        s.kind = QStringLiteral("text");
        s.text = words;
        s.x = x;
        s.y = y;
        s.style.fillKind = QStringLiteral("none");
        s.style.strokeWidth = 0;
        s.style.fontSize = size;
        s.style.bold = bold;
        s.style.textColor = QColor(color);
        s.style.hAlign = QStringLiteral("left");
        s.style.vAlign = QStringLiteral("top");
        s.autoSize = width <= 0;
        const QSizeF need = Render::textSize(s, width > 0 ? width - 8 : 0);
        s.w = width > 0 ? width : need.width() + 10;
        s.h = need.height() + 8;
        return add(s);
    }

    QString note(double x, double y, const QString &words, const QString &fill = QStringLiteral("#ffe07a"), double turn = 0)
    {
        Shape s;
        s.kind = QStringLiteral("note");
        s.x = x;
        s.y = y;
        s.w = 170;
        s.h = 170;
        s.rotation = turn;
        s.text = words;
        s.style.fill = QColor(fill);
        s.style.strokeWidth = 0;
        s.style.shadow = true;
        s.style.fontSize = 15;
        return add(s);
    }

    QString link(const QString &from, const QString &to, const QString &route = QStringLiteral("elbow"), const QString &label = {},
                 const QString &color = QStringLiteral("#1d1d1f"), const QString &head = QStringLiteral("triangle"))
    {
        Shape s;
        s.kind = QStringLiteral("line");
        s.route = route;
        s.from = from;
        s.to = to;
        s.text = label;
        s.style.stroke = QColor(color);
        s.style.endArrow = head;
        s.style.fontSize = 13;
        s.style.textColor = QColor(QStringLiteral("#3a3a3c"));
        return add(s);
    }

    QString line(double x1, double y1, double x2, double y2, const QString &color, double width = 2, const QString &head = QStringLiteral("none"))
    {
        Shape s;
        s.kind = QStringLiteral("line");
        s.p1 = QPointF(x1, y1);
        s.p2 = QPointF(x2, y2);
        const QRectF r = QRectF(s.p1, s.p2).normalized();
        s.x = r.x();
        s.y = r.y();
        s.w = r.width();
        s.h = r.height();
        s.style.stroke = QColor(color);
        s.style.strokeWidth = width;
        s.style.endArrow = head;
        return add(s);
    }

    Shape &last() { return doc.shapes.last(); }
    Shape &at(const QString &id) { return *doc.find(id); }
};

const char *Teal = "#0f9aa8";
const char *Sky = "#d9f3f7";
const char *Sun = "#ffe07a";
const char *Rose = "#ffd6d9";
const char *Leaf = "#d7f0d0";
const char *Lilac = "#e4dcfa";

void flow(Builder &b)
{
    b.frame(QStringLiteral("Diagrama"), 0, 0, 1123, 794);
    b.text(60, 44, QStringLiteral("Proceso de pedido"), 30, true);
    b.text(60, 90, QStringLiteral("Arrastra las formas: los conectores las siguen."), 15, false, QStringLiteral("#6e6e73"));
    const QString start = b.shape(QStringLiteral("pill"), 100, 190, 170, 64, QStringLiteral("Inicio"), Leaf);
    const QString order = b.shape(QStringLiteral("rect"), 350, 174, 190, 96, QStringLiteral("Recibir pedido"), Sky, QStringLiteral("#1d1d1f"), 8);
    const QString stock = b.shape(QStringLiteral("diamond"), 620, 152, 200, 140, QStringLiteral("¿Hay existencias?"), Sun);
    const QString pack = b.shape(QStringLiteral("rect"), 900, 174, 170, 96, QStringLiteral("Preparar envío"), Sky, QStringLiteral("#1d1d1f"), 8);
    const QString buy = b.shape(QStringLiteral("parallelogram"), 625, 400, 190, 90, QStringLiteral("Pedir al proveedor"), Rose);
    const QString data = b.shape(QStringLiteral("cylinder"), 370, 390, 150, 110, QStringLiteral("Almacén"), Lilac);
    const QString bill = b.shape(QStringLiteral("document"), 900, 395, 170, 100, QStringLiteral("Factura"), QStringLiteral("#ffffff"));
    const QString end = b.shape(QStringLiteral("pill"), 900, 610, 170, 64, QStringLiteral("Fin"), Leaf);
    b.link(start, order);
    b.link(order, stock);
    b.link(stock, pack, QStringLiteral("elbow"), QStringLiteral("Sí"));
    b.link(stock, buy, QStringLiteral("elbow"), QStringLiteral("No"));
    b.link(buy, data);
    b.link(data, order, QStringLiteral("elbow"));
    b.last().style.dash = QStringLiteral("dash");
    b.link(pack, bill);
    b.link(bill, end);
}

void mindmap(Builder &b)
{
    const QString centre = b.shape(QStringLiteral("ellipse"), -120, -60, 240, 120, QStringLiteral("Idea central"), Teal, QString());
    b.at(centre).style.textColor = QColor(Qt::white);
    b.at(centre).style.fontSize = 22;
    b.at(centre).style.bold = true;
    b.at(centre).style.shadow = true;
    struct Branch { const char *name; double x; double y; const char *fill; const char *ink; const char *leaves[2]; };
    const Branch branches[] = {
        {"Objetivos", -520, -250, Sky, "#0f9aa8", {"Corto plazo", "Largo plazo"}},
        {"Personas", 340, -250, Rose, "#e0525e", {"Equipo", "Clientes"}},
        {"Recursos", -520, 170, Leaf, "#3d9a45", {"Presupuesto", "Herramientas"}},
        {"Riesgos", 340, 170, Lilac, "#7a5cd6", {"Plazos", "Dependencias"}},
    };
    for (const Branch &branch : branches) {
        const QString node = b.shape(QStringLiteral("rect"), branch.x, branch.y, 180, 70, QString::fromUtf8(branch.name), QString::fromLatin1(branch.fill),
                                     QString::fromLatin1(branch.ink), 35);
        b.at(node).style.bold = true;
        b.link(centre, node, QStringLiteral("curve"), {}, QString::fromLatin1(branch.ink), QStringLiteral("none"));
        b.last().style.strokeWidth = 3;
        const bool left = branch.x < 0;
        for (int i = 0; i < 2; ++i) {
            const QString leaf = b.shape(QStringLiteral("rect"), left ? branch.x - 240 : branch.x + 260, branch.y - 40 + i * 90, 160, 50, QString::fromUtf8(branch.leaves[i]),
                                         QStringLiteral("#ffffff"), QString::fromLatin1(branch.ink), 25);
            b.at(leaf).style.strokeWidth = 1.5;
            b.at(leaf).style.fontSize = 14;
            b.link(node, leaf, QStringLiteral("curve"), {}, QString::fromLatin1(branch.ink), QStringLiteral("none"));
        }
    }
}

void wireframe(Builder &b)
{
    b.frame(QStringLiteral("Escritorio"), 0, 0, 1280, 720);
    const QString grey = QStringLiteral("#e9e9ee");
    const QString edge = QStringLiteral("#c7c7cc");
    b.shape(QStringLiteral("rect"), 0, 0, 1280, 64, {}, QStringLiteral("#f6f6f8"), QString());
    b.shape(QStringLiteral("ellipse"), 32, 16, 32, 32, {}, Teal, QString());
    b.text(78, 20, QStringLiteral("Marca"), 18, true);
    for (int i = 0; i < 3; ++i) {
        b.shape(QStringLiteral("rect"), 840 + i * 110, 26, 80, 12, {}, grey, QString(), 6);
    }
    b.shape(QStringLiteral("rect"), 1170, 16, 80, 32, QStringLiteral("Entrar"), QStringLiteral("#1d1d1f"), QString(), 16);
    b.last().style.textColor = QColor(Qt::white);
    b.last().style.fontSize = 13;
    b.text(80, 150, QStringLiteral("Un titular claro\nen dos líneas"), 46, true);
    b.shape(QStringLiteral("rect"), 84, 290, 420, 12, {}, grey, QString(), 6);
    b.shape(QStringLiteral("rect"), 84, 316, 360, 12, {}, grey, QString(), 6);
    b.shape(QStringLiteral("rect"), 84, 370, 170, 48, QStringLiteral("Empezar"), Teal, QString(), 24);
    b.last().style.textColor = QColor(Qt::white);
    b.last().style.bold = true;
    b.shape(QStringLiteral("rect"), 270, 370, 170, 48, QStringLiteral("Saber más"), QStringLiteral("#ffffff"), edge, 24);
    b.shape(QStringLiteral("rect"), 660, 120, 540, 330, {}, grey, edge, 14);
    b.line(660, 120, 1200, 450, edge, 1.5);
    b.line(1200, 120, 660, 450, edge, 1.5);
    for (int i = 0; i < 3; ++i) {
        const double x = 80 + i * 384;
        b.shape(QStringLiteral("rect"), x, 510, 352, 160, {}, QStringLiteral("#ffffff"), edge, 14);
        b.shape(QStringLiteral("ellipse"), x + 24, 534, 40, 40, {}, Sky, QString());
        b.shape(QStringLiteral("rect"), x + 24, 596, 200, 12, {}, QStringLiteral("#c7c7cc"), QString(), 6);
        b.shape(QStringLiteral("rect"), x + 24, 622, 290, 10, {}, grey, QString(), 5);
    }
    b.frame(QStringLiteral("Móvil"), 1400, 0, 390, 720);
    b.shape(QStringLiteral("rect"), 1400, 0, 390, 64, {}, QStringLiteral("#f6f6f8"), QString());
    b.shape(QStringLiteral("ellipse"), 1424, 16, 32, 32, {}, Teal, QString());
    b.shape(QStringLiteral("rect"), 1424, 100, 342, 200, {}, grey, edge, 14);
    b.text(1424, 326, QStringLiteral("Un titular claro"), 28, true);
    b.shape(QStringLiteral("rect"), 1426, 380, 300, 10, {}, grey, QString(), 5);
    b.shape(QStringLiteral("rect"), 1426, 402, 240, 10, {}, grey, QString(), 5);
    b.shape(QStringLiteral("rect"), 1424, 446, 342, 48, QStringLiteral("Empezar"), Teal, QString(), 24);
    b.last().style.textColor = QColor(Qt::white);
    b.last().style.bold = true;
}

void poster(Builder &b)
{
    b.frame(QStringLiteral("Póster"), 0, 0, 794, 1123, QStringLiteral("#fbf7ef"));
    const QString sun = b.shape(QStringLiteral("ellipse"), 470, 262, 290, 290, {}, QStringLiteral("#ff9f43"), QString());
    b.at(sun).style.fillKind = QStringLiteral("linear");
    b.at(sun).style.fill2 = QColor(QStringLiteral("#ff5e7e"));
    b.at(sun).style.gradientAngle = 60;
    b.shape(QStringLiteral("rect"), 0, 560, 794, 563, {}, QStringLiteral("#0f9aa8"), QString());
    b.last().style.fillKind = QStringLiteral("linear");
    b.last().style.fill2 = QColor(QStringLiteral("#0b5c7a"));
    b.shape(QStringLiteral("triangle"), 0, 300, 500, 260, {}, QStringLiteral("#12314a"), QString());
    b.shape(QStringLiteral("triangle"), 270, 370, 400, 190, {}, QStringLiteral("#1c4a66"), QString());
    b.text(60, 70, QStringLiteral("FESTIVAL\nDE VERANO"), 76, true, QStringLiteral("#12314a"));
    b.text(60, 760, QStringLiteral("Música, cine y talleres\nal aire libre"), 38, true, QStringLiteral("#ffffff"));
    b.text(60, 900, QStringLiteral("Del 12 al 14 de julio · Parque del Río"), 22, false, QStringLiteral("#d9f3f7"));
    b.shape(QStringLiteral("rect"), 60, 980, 240, 60, QStringLiteral("Entrada libre"), QStringLiteral("#ffe07a"), QString(), 30);
    b.last().style.bold = true;
    b.last().style.fontSize = 20;
    const QString star = b.shape(QStringLiteral("star"), 610, 900, 130, 130, {}, QStringLiteral("#ffe07a"), QString());
    b.at(star).rotation = 12;
}

void sample(Builder &b)
{
    flow(b);
    b.doc.title = QStringLiteral("Muestra");
    b.note(1190, 60, QStringLiteral("Revisar los plazos con el proveedor"), QString::fromLatin1(Sun), -4);
    b.note(1390, 90, QStringLiteral("Las notas son para ideas sueltas"), QStringLiteral("#bfe9ff"), 3);
    const QString star = b.shape(QStringLiteral("star"), 1210, 320, 150, 150, {}, QStringLiteral("#ff9f43"));
    b.at(star).style.fillKind = QStringLiteral("linear");
    b.at(star).style.fill2 = QColor(QStringLiteral("#ff5e7e"));
    b.at(star).style.shadow = true;
    const QString bubble = b.shape(QStringLiteral("bubble"), 1400, 320, 190, 130, QStringLiteral("¡Hola!"), QStringLiteral("#ffffff"), QString::fromLatin1(Teal));
    b.at(bubble).style.fontSize = 22;
    b.at(bubble).style.bold = true;
    b.shape(QStringLiteral("arrowRight"), 1210, 540, 180, 90, {}, QString::fromLatin1(Teal), QString());
    b.last().style.opacity = 0.85;
    Shape wave;
    wave.kind = QStringLiteral("path");
    wave.path = Model::pathFromD(QStringLiteral("M0 40 C 30 0 60 0 90 40 S 150 80 180 40"));
    wave.ref = wave.path.boundingRect();
    wave.x = 1410;
    wave.y = 560;
    wave.w = wave.ref.width();
    wave.h = wave.ref.height();
    wave.style.fillKind = QStringLiteral("none");
    wave.style.stroke = QColor(QStringLiteral("#7a5cd6"));
    wave.style.strokeWidth = 4;
    b.add(wave);
    b.doc.palette = {QStringLiteral("#0f9aa8"), QStringLiteral("#ffe07a"), QStringLiteral("#ff5e7e"), QStringLiteral("#7a5cd6"), QStringLiteral("#1d1d1f")};
}
}

QVariantList Templates::list()
{
    const auto entry = [](const char *key, const char *name, const char *detail) {
        return QVariantMap{{QStringLiteral("key"), QString::fromLatin1(key)}, {QStringLiteral("name"), QString::fromUtf8(name)}, {QStringLiteral("detail"), QString::fromUtf8(detail)}};
    };
    return {
        entry("blank", "En blanco", "Lienzo vacío"),
        entry("flow", "Diagrama de flujo", "Formas y conectores"),
        entry("mindmap", "Mapa mental", "Ideas en ramas"),
        entry("wireframe", "Wireframe", "Escritorio y móvil"),
        entry("poster", "Póster", "A4 vertical"),
    };
}

Document Templates::make(const QString &key)
{
    Builder b;
    b.doc.uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    b.doc.title = QStringLiteral("Sin título");
    for (const QVariant &v : list()) {
        const QVariantMap entry = v.toMap();
        if (entry.value(QStringLiteral("key")).toString() == key && key != QLatin1String("blank")) {
            b.doc.title = entry.value(QStringLiteral("name")).toString();
        }
    }
    if (key == QLatin1String("flow")) {
        flow(b);
    } else if (key == QLatin1String("mindmap")) {
        mindmap(b);
    } else if (key == QLatin1String("wireframe")) {
        wireframe(b);
    } else if (key == QLatin1String("poster")) {
        poster(b);
    } else if (key == QLatin1String("sample")) {
        sample(b);
    }
    Geometry::updateConnectors(b.doc);
    return b.doc;
}
