#include "docmodel.h"

#include <QTest>

class TestDocModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void styles()
    {
        QStringList ids;
        for (const Doc::Style &style : Doc::styles()) {
            ids << style.id;
        }
        QCOMPARE(ids, QStringList({"title", "h1", "h2", "h3", "body", "quote", "code"}));
        QCOMPARE(Doc::style(QStringLiteral("h2")).heading, 2);
        QCOMPARE(Doc::style(QStringLiteral("nobody")).id, QStringLiteral("body"));
        QVERIFY(Doc::lookOf(Doc::style(QStringLiteral("h1")).chars).bold);
        QVERIFY(Doc::lookOf(Doc::style(QStringLiteral("quote")).chars).italic);
        QCOMPARE(Doc::lookOf(Doc::style(QStringLiteral("code")).chars).family, Doc::monoFamily());
    }

    void applyStyleKeepsAlignmentAndLinks()
    {
        QTextDocument document;
        Doc::prepare(&document);
        QTextCursor cursor(&document);
        Doc::applyStyle(cursor, QStringLiteral("body"));
        cursor.insertText(QStringLiteral("Hola "));
        cursor.insertText(QStringLiteral("enlace"), Doc::linkFormat(QStringLiteral("https://example.org/")));
        QTextBlockFormat centred;
        centred.setAlignment(Qt::AlignHCenter);
        cursor.mergeBlockFormat(centred);

        Doc::applyStyle(cursor, QStringLiteral("h1"));
        const QTextBlock block = document.firstBlock();
        QCOMPARE(Doc::styleOf(block), QStringLiteral("h1"));
        QCOMPARE(Doc::outlineLevel(block), 1);
        QVERIFY(block.blockFormat().alignment() & Qt::AlignHCenter);
        auto it = block.begin();
        QCOMPARE(Doc::lookOf(it.fragment().charFormat()).size, 20.0);
        QVERIFY(Doc::lookOf(it.fragment().charFormat()).bold);
        ++it;
        QVERIFY(it.fragment().charFormat().isAnchor());
        QCOMPARE(it.fragment().charFormat().anchorHref(), QStringLiteral("https://example.org/"));
        QCOMPARE(Doc::lookOf(it.fragment().charFormat()).size, 20.0);
        QVERIFY(Doc::lookOf(it.fragment().charFormat()).color != Doc::ink());

        Doc::applyStyle(cursor, QStringLiteral("body"));
        QCOMPARE(Doc::outlineLevel(document.firstBlock()), -1);
        QVERIFY(!Doc::lookOf(document.firstBlock().begin().fragment().charFormat()).bold);
    }

    void titleLeadsTheOutline()
    {
        QTextDocument document;
        Doc::prepare(&document);
        QTextCursor cursor(&document);
        Doc::applyStyle(cursor, QStringLiteral("title"));
        QCOMPARE(Doc::outlineLevel(document.firstBlock()), 0);
        Doc::tagStyle(cursor, QStringLiteral("h3"));
        QCOMPARE(Doc::outlineLevel(document.firstBlock()), 3);
    }

    void pageSetup()
    {
        Doc::PageSetup setup;
        QCOMPARE(setup.paper, QStringLiteral("A4"));
        QCOMPARE(qRound(setup.pagePx().width()), 794);
        QCOMPARE(qRound(setup.pagePx().height()), 1123);
        QCOMPARE(qRound(setup.contentPx().width()), qRound((21.0 - 5.0) * Doc::pxPerCm));
        setup.landscape = true;
        QVERIFY(setup.pagePx().width() > setup.pagePx().height());

        setup.header = QStringLiteral("Cabecera");
        setup.pageNumbers = true;
        setup.left = 3;
        QCOMPARE(Doc::PageSetup::fromMap(setup.toMap()), setup);

        Doc::PageSetup letter;
        letter.setSizeCm(27.94, 21.59);
        QCOMPARE(letter.paper, QStringLiteral("Carta"));
        QVERIFY(letter.landscape);
        Doc::PageSetup odd;
        odd.setSizeCm(21.1, 29.6);
        QCOMPARE(odd.paper, QStringLiteral("A4"));
        QVERIFY(!odd.landscape);
        QVERIFY(Doc::PageSetup::papers().contains(QStringLiteral("Legal")));
    }

    void images()
    {
        QTextDocument document;
        Doc::prepare(&document);
        QImage picture(8, 4, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        const QString first = Doc::addImage(&document, picture);
        const QString second = Doc::addImage(&document, picture);
        QVERIFY(first != second);
        QCOMPARE(Doc::image(&document, first).size(), QSize(8, 4));
        QVERIFY(Doc::image(&document, QStringLiteral("nothing")).isNull());
    }

    void tableFormat()
    {
        const QTextTableFormat format = Doc::tableFormat(4);
        QCOMPARE(format.columnWidthConstraints().size(), 4);
        QCOMPARE(format.columnWidthConstraints().first().rawValue(), 25.0);
    }
};

QTEST_MAIN(TestDocModel)
#include "tst_docmodel.moc"
