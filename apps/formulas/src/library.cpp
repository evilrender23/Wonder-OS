#include "library.h"

#include "store.h"
#include "symbols.h"

#include <QJsonArray>
#include <QUuid>

namespace {

QString path(const QString &dataDir)
{
    return dataDir + QStringLiteral("/library.json");
}

void write(const QString &dataDir, const QList<Library::Entry> &entries)
{
    QJsonArray list;
    for (const Library::Entry &e : entries) {
        list.append(QJsonObject{{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name}, {QStringLiteral("latex"), e.latex}});
    }
    Store::write(path(dataDir), QJsonObject{{QStringLiteral("formulas"), list}});
}

Library::Shelf shelf(const char *id, const char *title, std::initializer_list<std::pair<const char *, const char *>> items)
{
    Library::Shelf out{QString::fromLatin1(id), QString::fromUtf8(title), {}};
    int n = 0;
    for (const auto &item : items) {
        out.entries.append({QStringLiteral("%1-%2").arg(out.id).arg(++n), QString::fromUtf8(item.first), QString::fromUtf8(item.second)});
    }
    return out;
}

}

const QList<Library::Shelf> &Library::collection()
{
    static const QList<Shelf> list = {
        shelf("algebra", "Álgebra", {
            {"Ecuación de segundo grado", R"(x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a})"},
            {"Binomio de Newton", R"((a + b)^n = \sum_{k=0}^{n} \binom{n}{k} a^{n-k} b^k)"},
            {"Identidad de Euler", R"(e^{i\pi} + 1 = 0)"},
            {"Suma de una progresión geométrica", R"(\sum_{k=0}^{n} r^k = \frac{1 - r^{n+1}}{1 - r})"},
            {"Determinante 2×2", R"(\begin{vmatrix} a & b \\ c & d \end{vmatrix} = ad - bc)"},
            {"Cambio de base del logaritmo", R"(\log_a x = \frac{\ln x}{\ln a})"},
            {"Diferencia de cuadrados", R"(a^2 - b^2 = (a + b)(a - b))"},
            {"Valor absoluto", R"(|x| = \begin{cases} x & \text{si } x \geq 0 \\ -x & \text{si } x < 0 \end{cases})"},
        }),
        shelf("calculus", "Cálculo", {
            {"Definición de derivada", R"(f'(x) = \lim_{h \to 0} \frac{f(x + h) - f(x)}{h})"},
            {"Teorema fundamental del cálculo", R"(\int_{a}^{b} f'(x) \, dx = f(b) - f(a))"},
            {"Integral gaussiana", R"(\int_{-\infty}^{\infty} e^{-x^2} \, dx = \sqrt{\pi})"},
            {"Serie de Taylor", R"(f(x) = \sum_{n=0}^{\infty} \frac{f^{(n)}(a)}{n!} (x - a)^n)"},
            {"Integración por partes", R"(\int u \, dv = uv - \int v \, du)"},
            {"Regla de la cadena", R"(\frac{dy}{dx} = \frac{dy}{du} \cdot \frac{du}{dx})"},
            {"Transformada de Fourier", R"(\hat{f}(\xi) = \int_{-\infty}^{\infty} f(x) \, e^{-2\pi i x \xi} \, dx)"},
            {"Teorema de Stokes", R"(\oint_{\partial S} \mathbf{F} \cdot d\mathbf{r} = \iint_{S} (\nabla \times \mathbf{F}) \cdot d\mathbf{S})"},
        }),
        shelf("physics", "Física", {
            {"Segunda ley de Newton", R"(\vec{F} = m \vec{a})"},
            {"Equivalencia entre masa y energía", R"(E = mc^2)"},
            {"Ecuación de Schrödinger", R"(i\hbar \frac{\partial}{\partial t} \Psi = \hat{H} \Psi)"},
            {"Ley de la gravitación universal", R"(F = G \frac{m_1 m_2}{r^2})"},
            {"Ley de Gauss", R"(\nabla \cdot \mathbf{E} = \frac{\rho}{\varepsilon_0})"},
            {"Ley de Faraday", R"(\nabla \times \mathbf{E} = -\frac{\partial \mathbf{B}}{\partial t})"},
            {"Factor de Lorentz", R"(\gamma = \frac{1}{\sqrt{1 - \frac{v^2}{c^2}}})"},
            {"Ecuación de los gases ideales", R"(PV = nRT)"},
        }),
        shelf("statistics", "Estadística", {
            {"Distribución normal", R"(f(x) = \frac{1}{\sigma \sqrt{2\pi}} \, e^{-\frac{1}{2} \left( \frac{x - \mu}{\sigma} \right)^2})"},
            {"Media aritmética", R"(\bar{x} = \frac{1}{n} \sum_{i=1}^{n} x_i)"},
            {"Varianza muestral", R"(s^2 = \frac{1}{n - 1} \sum_{i=1}^{n} (x_i - \bar{x})^2)"},
            {"Teorema de Bayes", R"(P(A \mid B) = \frac{P(B \mid A) \, P(A)}{P(B)})"},
            {"Distribución binomial", R"(P(X = k) = \binom{n}{k} p^k (1 - p)^{n-k})"},
            {"Coeficiente de correlación", R"(r = \frac{\sum (x_i - \bar{x})(y_i - \bar{y})}{\sqrt{\sum (x_i - \bar{x})^2 \sum (y_i - \bar{y})^2}})"},
            {"Distribución de Poisson", R"(P(X = k) = \frac{\lambda^k e^{-\lambda}}{k!})"},
            {"Esperanza", R"(\mathbb{E}[X] = \int_{-\infty}^{\infty} x \, f(x) \, dx)"},
        }),
    };
    return list;
}

QString Library::builtin(const QString &id)
{
    for (const Shelf &s : collection()) {
        for (const Entry &e : s.entries) {
            if (e.id == id) {
                return e.latex;
            }
        }
    }
    return {};
}

QList<Library::Entry> Library::saved(const QString &dataDir)
{
    QList<Entry> out;
    for (const QJsonValue &value : Store::read(path(dataDir)).value(QLatin1String("formulas")).toArray()) {
        const QJsonObject o = value.toObject();
        const Entry e{o.value(QLatin1String("id")).toString(), o.value(QLatin1String("name")).toString(), o.value(QLatin1String("latex")).toString()};
        if (!e.id.isEmpty() && !e.latex.trimmed().isEmpty()) {
            out.append(e);
        }
    }
    return out;
}

QString Library::save(const QString &dataDir, const QString &name, const QString &latex)
{
    if (latex.trimmed().isEmpty()) {
        return {};
    }
    QList<Entry> entries = saved(dataDir);
    Entry e{QUuid::createUuid().toString(QUuid::WithoutBraces), name.trimmed(), latex};
    if (e.name.isEmpty()) {
        e.name = QStringLiteral("Fórmula %1").arg(entries.size() + 1);
    }
    entries.append(e);
    write(dataDir, entries);
    return e.id;
}

bool Library::rename(const QString &dataDir, const QString &id, const QString &name)
{
    QList<Entry> entries = saved(dataDir);
    for (Entry &e : entries) {
        if (e.id == id && !name.trimmed().isEmpty()) {
            e.name = name.trimmed();
            write(dataDir, entries);
            return true;
        }
    }
    return false;
}

bool Library::remove(const QString &dataDir, const QString &id)
{
    QList<Entry> entries = saved(dataDir);
    const qsizetype removed = entries.removeIf([&id](const Entry &e) { return e.id == id; });
    if (removed > 0) {
        write(dataDir, entries);
    }
    return removed > 0;
}

QVariantList Library::shelves(const QString &dataDir, const QString &query)
{
    const QStringList words = Symbols::fold(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto take = [&words](const Shelf &s, bool own) {
        QVariantList entries;
        for (const Entry &e : s.entries) {
            const QString hay = Symbols::fold(e.name + QLatin1Char(' ') + s.title + QLatin1Char(' ') + e.latex);
            bool all = true;
            for (const QString &word : words) {
                all = all && hay.contains(word);
            }
            if (all) {
                entries << QVariantMap{{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name}, {QStringLiteral("latex"), e.latex}, {QStringLiteral("own"), own}};
            }
        }
        return QVariantMap{{QStringLiteral("id"), s.id}, {QStringLiteral("title"), s.title}, {QStringLiteral("entries"), entries}, {QStringLiteral("own"), own}};
    };
    QVariantList out;
    const QVariantMap mine = take(Shelf{QStringLiteral("mine"), QStringLiteral("Mis fórmulas"), saved(dataDir)}, true);
    // The user's shelf shows even when empty (it says how to fill it), unless a search hides it.
    if (words.isEmpty() || !mine.value(QStringLiteral("entries")).toList().isEmpty()) {
        out << mine;
    }
    for (const Shelf &s : collection()) {
        const QVariantMap map = take(s, false);
        if (!map.value(QStringLiteral("entries")).toList().isEmpty()) {
            out << map;
        }
    }
    return out;
}
