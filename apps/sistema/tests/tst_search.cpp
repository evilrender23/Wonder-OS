#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QTest>

/// The text of a QML `.js` library without its `.pragma` / `.import` lines, which only QML reads.
static QString library(const QString &name)
{
    QFile file(QStringLiteral(QML_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (QString &line : lines) {
        if (line.startsWith(QLatin1Char('.'))) {
            line.clear();
        }
    }
    return lines.join(QLatin1Char('\n'));
}

class SearchTest : public QObject
{
    Q_OBJECT

private:
    QJSEngine m_engine;
    QJSValue m_lib;

    QJSValue find(const QString &query)
    {
        return m_lib.property(QStringLiteral("find")).call({m_lib.property(QStringLiteral("pages")), query});
    }
    /// The config.toml keys of the settings a query finds.
    QStringList keys(const QString &query)
    {
        QStringList out;
        const QJSValue groups = find(query).property(QStringLiteral("groups"));
        for (int g = 0; g < groups.property(QStringLiteral("length")).toInt(); ++g) {
            const QJSValue items = groups.property(g).property(QStringLiteral("items"));
            for (int i = 0; i < items.property(QStringLiteral("length")).toInt(); ++i) {
                out << items.property(i).property(QStringLiteral("path")).call(QJSValueList()).toString();
            }
        }
        return out;
    }
    QStringList pages(const QString &query)
    {
        QStringList out;
        const QJSValue found = find(query).property(QStringLiteral("pages"));
        for (int i = 0; i < found.property(QStringLiteral("length")).toInt(); ++i) {
            out << found.property(i).property(QStringLiteral("title")).toString();
        }
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        const QString schema = library(QStringLiteral("Schema.js"));
        const QString search = library(QStringLiteral("Search.js"));
        QVERIFY(!schema.isEmpty());
        QVERIFY(!search.isEmpty());
        // `path` is handed back as a function so a key reads as "a.b.c" whatever the engine does with arrays.
        m_lib = m_engine.evaluate(QStringLiteral("(function(){\n") + schema + QLatin1Char('\n') + search
            + QStringLiteral("\nfor (const p of pages) for (const g of (p.groups || [])) for (const s of (g.items || []))"
                             " { const key = s.path.join('.'); s.key = key }\n"
                             "const wrap = (ps, q) => { const r = find(ps, q);"
                             " for (const g of r.groups) g.items = g.items.map(s => ({ path: () => s.key, label: s.label }));"
                             " return r }\n"
                             "return { pages: pages, find: wrap, fold: fold } })()"));
        QVERIFY2(!m_lib.isError(), qPrintable(m_lib.toString()));
    }

    void ignoresCaseAndAccents()
    {
        QCOMPARE(m_lib.property(QStringLiteral("fold")).call({QStringLiteral("Ratón y Panel TÁCTIL")}).toString(),
                 QStringLiteral("raton y panel tactil"));
        const QStringList found = keys(QStringLiteral("RATON"));
        QVERIFY(found.contains(QStringLiteral("input.mouse.accel_speed")));
        QVERIFY(found.contains(QStringLiteral("navigation.mouse_speed")));
    }

    void findsByConfigKey()
    {
        QCOMPARE(keys(QStringLiteral("repeat_rate")), QStringList{QStringLiteral("input.keyboard.repeat_rate")});
        QCOMPARE(keys(QStringLiteral("effects.glass.tint_strength")), QStringList{QStringLiteral("effects.glass.tint_strength")});
    }

    void findsByOptionAndDescription()
    {
        QVERIFY(keys(QStringLiteral("rejilla de puntos")).contains(QStringLiteral("background.type")));
        QVERIFY(keys(QStringLiteral("gelatina")).isEmpty());   // only in a group's note, which is not a setting
        QVERIFY(keys(QStringLiteral("estela")).contains(QStringLiteral("effects.cursor_blur.strength")));
    }

    void everyWordHasToMatch()
    {
        const QStringList delays = keys(QStringLiteral("retardo"));
        QVERIFY(delays.contains(QStringLiteral("input.keyboard.repeat_delay")));
        QVERIFY(delays.contains(QStringLiteral("navigation.edge_pan.latency_ms")));
        // The page a setting is on counts as one of its words.
        QCOMPARE(keys(QStringLiteral("retardo teclado")), QStringList{QStringLiteral("input.keyboard.repeat_delay")});
        QVERIFY(keys(QStringLiteral("retardo xyzzy")).isEmpty());
    }

    void aPageNameFindsThePageNotAllItHolds()
    {
        QCOMPARE(pages(QStringLiteral("avanzado")), QStringList{QStringLiteral("Avanzado")});
        QVERIFY(keys(QStringLiteral("avanzado")).isEmpty());
        QVERIFY(pages(QStringLiteral("atajos")).contains(QStringLiteral("Atajos de teclado")));
        QVERIFY(pages(QStringLiteral("carpetas")).contains(QStringLiteral("Carpetas")));
        QVERIFY(pages(QStringLiteral("color carpeta")).contains(QStringLiteral("Carpetas")));
        // …but it narrows a search down to that page.
        QCOMPARE(keys(QStringLiteral("teclado variante")), QStringList{QStringLiteral("input.keyboard.variant")});
    }

    void findsTablesOfBindings()
    {
        const QJSValue groups = find(QStringLiteral("variables de entorno")).property(QStringLiteral("groups"));
        QCOMPARE(groups.property(QStringLiteral("length")).toInt(), 1);
        QVERIFY(groups.property(0).property(QStringLiteral("table")).toBool());
        QCOMPARE(groups.property(0).property(QStringLiteral("pageTitle")).toString(), QStringLiteral("Avanzado"));
    }

    void nothingForAnEmptyOrUnknownQuery()
    {
        for (const QString &query : {QString(), QStringLiteral("   "), QStringLiteral("xyzzy")}) {
            const QJSValue found = find(query);
            QCOMPARE(found.property(QStringLiteral("pages")).property(QStringLiteral("length")).toInt(), 0);
            QCOMPARE(found.property(QStringLiteral("groups")).property(QStringLiteral("length")).toInt(), 0);
        }
    }

    /// No setting is out of reach: its own name finds it, and so does its key.
    void everySettingIsFound()
    {
        const QJSValue all = m_engine.evaluate(QStringLiteral(
            "(function(pages){ const out = []; for (const p of pages) for (const g of (p.groups || []))"
            " for (const s of (g.items || [])) out.push([s.key, s.label]); return out })")).call({m_lib.property(QStringLiteral("pages"))});
        const int count = all.property(QStringLiteral("length")).toInt();
        QVERIFY(count > 100);
        for (int i = 0; i < count; ++i) {
            const QString key = all.property(i).property(0).toString();
            const QString label = all.property(i).property(1).toString();
            QVERIFY2(keys(label).contains(key), qPrintable(label));
            QVERIFY2(keys(key).contains(key), qPrintable(key));
        }
    }
};

QTEST_GUILESS_MAIN(SearchTest)
#include "tst_search.moc"
