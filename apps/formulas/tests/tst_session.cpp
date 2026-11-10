#include "io.h"
#include "library.h"
#include "session.h"
#include "symbols.h"
#include "latex.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class TestSession : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsWithOneEquation()
    {
        Session session;
        QCOMPARE(session.count(), 1);
        QCOMPARE(session.current(), 0);
        QVERIFY(session.latex().isEmpty());
        QVERIFY(!session.dirty());
        QVERIFY(!session.canUndo());
        QVERIFY(!session.canRedo());
        QVERIFY(!session.undo());
    }

    void typingIsOneStep()
    {
        Session session;
        session.setLatex(QStringLiteral("x"));
        session.setLatex(QStringLiteral("x^"));
        session.setLatex(QStringLiteral("x^2"));
        QVERIFY(session.dirty());
        QVERIFY(session.undo());
        QVERIFY(session.latex().isEmpty());
        QVERIFY(session.redo());
        QCOMPARE(session.latex(), QStringLiteral("x^2"));
        // A pause, a command or a move elsewhere starts another.
        session.breakRun();
        session.setLatex(QStringLiteral("x^2 + 1"));
        session.undo();
        QCOMPARE(session.latex(), QStringLiteral("x^2"));
        session.undo();
        QVERIFY(session.latex().isEmpty());
        QVERIFY(!session.canUndo());
        // Writing the same is no change.
        session.setLatex(QString());
        QVERIFY(!session.canUndo());
    }

    void equations()
    {
        Session session;
        session.setLatex(QStringLiteral("a"));
        session.add(QStringLiteral("b"));
        QCOMPARE(session.count(), 2);
        QCOMPARE(session.current(), 1);
        session.setCurrent(0);
        session.add(QStringLiteral("c"));
        // After the one in hand.
        QCOMPARE(session.document().equations, (QStringList{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));
        QCOMPARE(session.current(), 1);
        session.duplicate();
        QCOMPARE(session.document().equations, (QStringList{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("c"), QStringLiteral("b")}));
        QCOMPARE(session.current(), 2);
        session.move(2, 0);
        QCOMPARE(session.document().equations, (QStringList{QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));
        QCOMPARE(session.current(), 0);
        // Past the end is the end.
        session.move(0, 99);
        QCOMPARE(session.document().equations, (QStringList{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(session.current(), 3);
        session.remove(3);
        QCOMPARE(session.count(), 3);
        QVERIFY(session.current() < session.count());
        session.setCurrent(99);
        QVERIFY(session.current() < session.count());
        session.setCurrent(-4);
        QVERIFY(session.current() >= 0);
        // Each of those is a step back.
        session.undo();
        QCOMPARE(session.count(), 4);
    }

    void theLastOneIsEmptiedNotRemoved()
    {
        Session session;
        session.setLatex(QStringLiteral("x"));
        session.remove(0);
        QCOMPARE(session.count(), 1);
        QVERIFY(session.latex().isEmpty());
        session.undo();
        QCOMPARE(session.latex(), QStringLiteral("x"));
    }

    void format()
    {
        Session session;
        session.setSize(30);
        session.setColor(QStringLiteral("#ff0000"));
        session.setFont(QStringLiteral("STIX Two Math"));
        session.setTitle(QStringLiteral("Uno"));
        QCOMPARE(session.document().size, 30.0);
        QCOMPARE(session.document().color, QStringLiteral("#ff0000"));
        QCOMPARE(session.document().title, QStringLiteral("Uno"));
        QVERIFY(session.dirty());
        session.undo();
        session.undo();
        session.undo();
        QVERIFY(session.document().color.isEmpty());
        QCOMPARE(session.document().size, 30.0);
        // Out of bounds sizes are brought in.
        session.setSize(1000);
        QVERIFY(session.document().size <= 144);
        session.setSize(0);
        QVERIFY(session.document().size >= 6);
    }

    void undoneThenChangedForgetsTheRedo()
    {
        Session session;
        session.setLatex(QStringLiteral("a"));
        session.breakRun();
        session.setLatex(QStringLiteral("ab"));
        session.undo();
        QVERIFY(session.canRedo());
        session.setLatex(QStringLiteral("ac"));
        QVERIFY(!session.canRedo());
    }

    void savedAndLoaded()
    {
        Session session;
        session.setLatex(QStringLiteral("a"));
        session.markSaved();
        QVERIFY(!session.dirty());
        session.setLatex(QStringLiteral("ab"));
        QVERIFY(session.dirty());
        FormulaDoc doc;
        doc.uid = QStringLiteral("u");
        doc.equations = {QStringLiteral("p"), QStringLiteral("q")};
        session.setDocument(doc);
        QVERIFY(!session.dirty());
        QVERIFY(!session.canUndo());
        QCOMPARE(session.count(), 2);
        // A version brought back replaces everything but who it is, and can be undone.
        FormulaDoc old;
        old.uid = QStringLiteral("other");
        old.equations = {QStringLiteral("z")};
        session.replace(old);
        QCOMPARE(session.document().uid, QStringLiteral("u"));
        QCOMPARE(session.document().equations, QStringList{QStringLiteral("z")});
        QVERIFY(session.dirty());
        session.undo();
        QCOMPARE(session.count(), 2);
        // A document with nothing in it still has an equation.
        session.setDocument(FormulaDoc());
        QCOMPARE(session.count(), 1);
    }

    void versions()
    {
        QTemporaryDir dir;
        FormulaDoc doc;
        doc.uid = QStringLiteral("doc1");
        const QDateTime start(QDate(2025, 3, 1), QTime(10, 0));
        for (int i = 0; i < Io::MaxVersions + 5; ++i) {
            doc.equations = {QStringLiteral("x = %1").arg(i)};
            const QString path = Io::addVersion(dir.path(), doc, start.addSecs(60 * i));
            QVERIFY(QFile::exists(path));
        }
        const QList<Io::Version> list = Io::versions(dir.path(), doc.uid);
        QCOMPARE(list.size(), Io::MaxVersions);
        // Newest first, and the oldest are the ones that went.
        QCOMPARE(list.first().when, start.addSecs(60 * (Io::MaxVersions + 4)));
        QCOMPARE(list.last().when, start.addSecs(60 * 5));
        FormulaDoc newest;
        QString error;
        QVERIFY(Formats::read(list.first().path, &newest, &error));
        QCOMPARE(newest.equations.first(), QStringLiteral("x = %1").arg(Io::MaxVersions + 4));
        // Each document has its own.
        QVERIFY(Io::versions(dir.path(), QStringLiteral("doc2")).isEmpty());
        QVERIFY(Io::versions(dir.path(), QString()).isEmpty());
    }

    void recovery()
    {
        QTemporaryDir dir;
        QVERIFY(Io::recoveries(dir.path()).isEmpty());
        FormulaDoc doc;
        doc.uid = QStringLiteral("doc1");
        doc.title = QStringLiteral("Apuntes");
        doc.equations = {QStringLiteral("a + b")};
        QVERIFY(Io::writeRecovery(dir.path(), doc, QStringLiteral("/home/x/apuntes.wform")));
        doc.equations = {QStringLiteral("a + b + c")};
        QVERIFY(Io::writeRecovery(dir.path(), doc, QStringLiteral("/home/x/apuntes.wform")));
        const QList<Io::Recovery> list = Io::recoveries(dir.path());
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().title, QStringLiteral("Apuntes"));
        QCOMPARE(list.first().origin, QStringLiteral("/home/x/apuntes.wform"));
        QVERIFY(list.first().when.isValid());
        FormulaDoc back;
        QString error;
        QVERIFY(Formats::read(list.first().path, &back, &error));
        QCOMPARE(back.equations, doc.equations);
        QCOMPARE(back.uid, doc.uid);
        Io::dropRecovery(dir.path(), doc.uid);
        QVERIFY(Io::recoveries(dir.path()).isEmpty());
    }

    void collection()
    {
        const QList<Library::Shelf> &shelves = Library::collection();
        QStringList titles;
        QSet<QString> ids;
        int count = 0;
        for (const Library::Shelf &shelf : shelves) {
            titles << shelf.title;
            QVERIFY(!shelf.entries.isEmpty());
            for (const Library::Entry &entry : shelf.entries) {
                ++count;
                QVERIFY2(!ids.contains(entry.id), qPrintable(entry.id));
                ids.insert(entry.id);
                QVERIFY(!entry.name.isEmpty());
                // Every one of them is understood whole.
                const Parsed parsed = Latex::parse(entry.latex);
                QVERIFY2(parsed.ok(), qPrintable(entry.name + QStringLiteral(": ") + parsed.errors.value(0).message));
                QCOMPARE(Library::builtin(entry.id), entry.latex);
            }
        }
        QCOMPARE(titles, (QStringList{QStringLiteral("Álgebra"), QStringLiteral("Cálculo"), QStringLiteral("Física"), QStringLiteral("Estadística")}));
        QVERIFY(count >= 24);
        QVERIFY(Library::builtin(QStringLiteral("nothing")).isEmpty());
    }

    void ownLibrary()
    {
        QTemporaryDir dir;
        QVERIFY(Library::saved(dir.path()).isEmpty());
        const QString first = Library::save(dir.path(), QStringLiteral("Mi fórmula"), QStringLiteral("a^2"));
        const QString second = Library::save(dir.path(), QString(), QStringLiteral("b^2"));
        QVERIFY(!first.isEmpty());
        QVERIFY(first != second);
        QList<Library::Entry> list = Library::saved(dir.path());
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).name, QStringLiteral("Mi fórmula"));
        QCOMPARE(list.at(0).latex, QStringLiteral("a^2"));
        QCOMPARE(list.at(1).name, QStringLiteral("Fórmula 2"));
        QVERIFY(Library::rename(dir.path(), second, QStringLiteral("Otra")));
        QVERIFY(!Library::rename(dir.path(), QStringLiteral("nope"), QStringLiteral("x")));
        QCOMPARE(Library::saved(dir.path()).at(1).name, QStringLiteral("Otra"));

        // The window's view of it: the user's shelf first, then the collection.
        QVariantList shelves = Library::shelves(dir.path());
        QCOMPARE(shelves.size(), Library::collection().size() + 1);
        QVERIFY(shelves.first().toMap().value(QStringLiteral("own")).toBool());
        QCOMPARE(shelves.first().toMap().value(QStringLiteral("entries")).toList().size(), 2);
        // The search does not mind accents or case, and looks in both.
        shelves = Library::shelves(dir.path(), QStringLiteral("OTRA"));
        QCOMPARE(shelves.first().toMap().value(QStringLiteral("entries")).toList().size(), 1);
        int found = 0;
        for (const QVariant &shelf : Library::shelves(dir.path(), QStringLiteral("SCHRODINGER"))) {
            found += shelf.toMap().value(QStringLiteral("entries")).toList().size();
        }
        QCOMPARE(found, 1);

        QVERIFY(Library::remove(dir.path(), first));
        QVERIFY(!Library::remove(dir.path(), first));
        QCOMPARE(Library::saved(dir.path()).size(), 1);
    }

    void symbolTable()
    {
        QCOMPARE(Symbols::find(QStringLiteral("alpha"))->code, U'α');
        QCOMPARE(Symbols::find(QStringLiteral("leq"))->atom, Atom::Rel);
        QVERIFY(Symbols::find(QStringLiteral("sum"))->limits);
        QVERIFY(!Symbols::find(QStringLiteral("int"))->limits);
        QVERIFY(!Symbols::find(QStringLiteral("nosuch")));
        QCOMPARE(QLatin1String(Symbols::byCode(U'∞')->command), QLatin1String("infty"));
        QVERIFY(Symbols::function(QStringLiteral("sin")));
        QVERIFY(Symbols::function(QStringLiteral("lim"))->limits);
        QVERIFY(Symbols::known(QStringLiteral("frac")));
        QVERIFY(Symbols::known(QStringLiteral("alpha")));
        QVERIFY(Symbols::known(QStringLiteral("cos")));
        QVERIFY(!Symbols::known(QStringLiteral("nosuch")));
        QCOMPARE(Symbols::fold(QStringLiteral("Raíz CÚBICA ñ")), QStringLiteral("raiz cubica n"));
        // Every command of the table is one the parser takes.
        QSet<QString> seen;
        for (const SymbolInfo &symbol : Symbols::all()) {
            const QString command = QString::fromLatin1(symbol.command);
            QVERIFY2(!seen.contains(command), symbol.command);
            seen.insert(command);
            QVERIFY2(Latex::parse(QLatin1Char('\\') + command).ok(), symbol.command);
            QVERIFY2(qstrlen(symbol.names) > 0, symbol.command);
        }
        QVERIFY(seen.size() > 200);
    }

    void paletteOfSymbols()
    {
        const QVariantList all = Symbols::palette(QString());
        QVERIFY(all.size() >= 12);
        QCOMPARE(all.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Estructuras"));
        int count = 0;
        for (const QVariant &group : all) {
            for (const QVariant &value : group.toMap().value(QStringLiteral("items")).toList()) {
                ++count;
                const QVariantMap item = value.toMap();
                // What it shows and what it writes are both understood.
                const QString shown = item.value(QStringLiteral("latex")).toString();
                const Parsed drawn = Latex::parse(shown);
                QVERIFY2(drawn.ok(), qPrintable(shown + QStringLiteral(": ") + drawn.errors.value(0).message));
                QString insert = item.value(QStringLiteral("insert")).toString();
                QVERIFY(insert.count(QLatin1Char('|')) <= 1 || insert.contains(QStringLiteral("\\|")) || insert.contains(QStringLiteral("vert")));
                QVERIFY(!item.value(QStringLiteral("title")).toString().isEmpty());
            }
        }
        QVERIFY2(count > 250, qPrintable(QString::number(count)));
    }

    void searchInSpanish_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("expected");
        QTest::newRow("integral") << "integral" << "\\int";
        QTest::newRow("flecha") << "flecha" << "\\to";
        QTest::newRow("alfa") << "alfa" << "\\alpha";
        QTest::newRow("accents") << "raíz" << "\\sqrt";
        QTest::newRow("no accents") << "fraccion" << "\\frac";
        QTest::newRow("infinito") << "INFINITO" << "\\infty";
        QTest::newRow("command") << "\\leq" << "\\leq";
        QTest::newRow("matriz") << "matriz" << "pmatrix";
        QTest::newRow("sumatorio") << "sumatorio" << "\\sum";
        QTest::newRow("pertenece") << "pertenece" << "\\in";
    }
    void searchInSpanish()
    {
        QFETCH(QString, query);
        QFETCH(QString, expected);
        QStringList found;
        for (const QVariant &group : Symbols::palette(query)) {
            for (const QVariant &item : group.toMap().value(QStringLiteral("items")).toList()) {
                found << item.toMap().value(QStringLiteral("insert")).toString();
            }
        }
        QVERIFY(!found.isEmpty());
        QVERIFY(found.size() < 60);
        bool hit = false;
        for (const QString &insert : found) {
            hit = hit || insert.contains(expected);
        }
        QVERIFY2(hit, qPrintable(found.join(QStringLiteral(" · "))));
        QVERIFY(Symbols::palette(QStringLiteral("zzzzqqq")).isEmpty());
    }

    void completions()
    {
        const QVariantList al = Symbols::completions(QStringLiteral("al"));
        QVERIFY(!al.isEmpty());
        QCOMPARE(al.first().toMap().value(QStringLiteral("command")).toString(), QStringLiteral("\\alpha"));
        QCOMPARE(al.first().toMap().value(QStringLiteral("glyph")).toString(), QString::fromUtf8("α"));
        QCOMPARE(al.first().toMap().value(QStringLiteral("insert")).toString(), QStringLiteral("\\alpha "));
        const QVariantList fr = Symbols::completions(QStringLiteral("fr"));
        QCOMPARE(fr.first().toMap().value(QStringLiteral("command")).toString(), QStringLiteral("\\frac"));
        QCOMPARE(fr.first().toMap().value(QStringLiteral("insert")).toString(), QStringLiteral("\\frac{|}{}"));
        // The shortest first, and no more than asked.
        QCOMPARE(Symbols::completions(QStringLiteral("in")).first().toMap().value(QStringLiteral("command")).toString(), QStringLiteral("\\in"));
        QCOMPARE(Symbols::completions(QStringLiteral("s"), 5).size(), 5);
        QVERIFY(Symbols::completions(QStringLiteral("zzz")).isEmpty());
        QVERIFY(Symbols::completions(QStringLiteral("si")).size() >= 3);
        // What a completion draws is understood.
        for (const QVariant &value : Symbols::completions(QString(), 2000)) {
            const QString shown = value.toMap().value(QStringLiteral("latex")).toString();
            if (!shown.isEmpty()) {
                QVERIFY2(Latex::parse(shown).ok(), qPrintable(shown));
            }
        }
    }
};

QTEST_MAIN(TestSession)
#include "tst_session.moc"
