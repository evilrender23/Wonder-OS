#include "tomldoc.h"

#include <QFile>
#include <QTest>

class TomlDocTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsValues()
    {
        TomlDoc doc;
        QVERIFY(doc.parse(QStringLiteral(
            "mod_key = \"super\"  # the modifier\n"
            "autostart = [\n  \"waybar\", # bar\n  'swaync',\n]\n"
            "[input.keyboard]\nrepeat_rate = 25\nlayout = \"us,ru\"\n"
            "[snap]\ngap = 12.5\nenabled = true\n"
            "[keybindings]\n\"mod+return\" = \"exec-terminal\"\n\"shift+Print\" = \"spawn grim -g \\\"$(slurp -d)\\\" - | wl-copy\"\n")));
        QCOMPARE(doc.value({"mod_key"}).toString(), QStringLiteral("super"));
        QCOMPARE(doc.value({"autostart"}).toStringList(), QStringList({"waybar", "swaync"}));
        QCOMPARE(doc.value({"input", "keyboard", "repeat_rate"}).toInt(), 25);
        QCOMPARE(doc.value({"snap", "gap"}).toDouble(), 12.5);
        QCOMPARE(doc.value({"snap", "enabled"}).typeId(), int(QMetaType::Bool));
        QCOMPARE(doc.value({"keybindings", "shift+Print"}).toString(), QStringLiteral("spawn grim -g \"$(slurp -d)\" - | wl-copy"));
        QCOMPARE(doc.keys({"keybindings"}), QStringList({"mod+return", "shift+Print"}));
        QVERIFY(!doc.contains({"snap", "distance"}));
    }

    void rejectsBrokenFiles()
    {
        TomlDoc doc;
        QString error;
        QVERIFY(!doc.parse(QStringLiteral("a = 1\nb = \"open\n"), &error));
        QVERIFY(error.startsWith(QLatin1String("line 2")));
        QVERIFY(!doc.parse(QStringLiteral("[snap\ngap = 1\n")));
        QVERIFY(!doc.parse(QStringLiteral("gap 1\n")));
    }

    void changesOnlyTheValue()
    {
        const QString text = QStringLiteral("# mine\n[snap]\n# the gap\ngap   =  12.0   # canvas px\nenabled = true\n");
        TomlDoc doc;
        QVERIFY(doc.parse(text));
        doc.set({"snap", "gap"}, 20, TomlDoc::Kind::Float);
        QCOMPARE(doc.text(), QStringLiteral("# mine\n[snap]\n# the gap\ngap   =  20.0   # canvas px\nenabled = true\n"));
        doc.set({"snap", "enabled"}, false);
        QCOMPARE(doc.value({"snap", "enabled"}).toBool(), false);
        QVERIFY(doc.text().contains(QLatin1String("enabled = false\n")));
    }

    void addsKeysWhereTheyBelong()
    {
        TomlDoc doc;
        QVERIFY(doc.parse(QStringLiteral("[snap]\n# about snapping\ngap = 1.0\n\n[zoom]\nstep = 1.2\n")));
        doc.set({"snap", "corners"}, false);
        QCOMPARE(doc.text(), QStringLiteral("[snap]\n# about snapping\ngap = 1.0\ncorners = false\n\n[zoom]\nstep = 1.2\n"));
        doc.set({"mod_key"}, QStringLiteral("alt"));
        QVERIFY(doc.text().startsWith(QLatin1String("mod_key = \"alt\"\n\n[snap]\n")));
        QCOMPARE(doc.value({"mod_key"}).toString(), QStringLiteral("alt"));
        doc.set({"input", "mouse", "left_handed"}, true);
        QVERIFY(doc.text().endsWith(QLatin1String("\n[input.mouse]\nleft_handed = true\n")));
        doc.set({"keybindings", "mod+shift+t"}, QStringLiteral("spawn kitty -e \"top\""));
        QVERIFY(doc.text().endsWith(QLatin1String("\n[keybindings]\n\"mod+shift+t\" = \"spawn kitty -e \\\"top\\\"\"\n")));
        TomlDoc again;
        QVERIFY(again.parse(doc.text()));
        QCOMPARE(again.value({"keybindings", "mod+shift+t"}).toString(), QStringLiteral("spawn kitty -e \"top\""));
        QCOMPARE(again.value({"snap", "corners"}).toBool(), false);
    }

    void fillsAnEmptyTableAndAnEmptyFile()
    {
        TomlDoc doc;
        QVERIFY(doc.parse(QStringLiteral("[env]\n# variables\n\n[zoom]\nstep = 1.2\n")));
        doc.set({"env", "QT_SCALE"}, QStringLiteral("1"));
        QCOMPARE(doc.text(), QStringLiteral("[env]\n# variables\nQT_SCALE = \"1\"\n\n[zoom]\nstep = 1.2\n"));

        TomlDoc empty;
        QVERIFY(empty.parse(QString()));
        empty.set({"focus_follows_mouse"}, true);
        empty.set({"zoom", "step"}, 1.5);
        QCOMPARE(empty.text(), QStringLiteral("focus_follows_mouse = true\n\n[zoom]\nstep = 1.5\n"));
    }

    void rootKeysNeverLandInsideATable()
    {
        TomlDoc doc;
        QVERIFY(doc.parse(QStringLiteral("# my config\n\n# snapping\n[snap]\ngap = 1.0\n")));
        doc.set({"mod_key"}, QStringLiteral("alt"));
        TomlDoc again;
        QVERIFY(again.parse(doc.text()));
        QCOMPARE(again.value({"mod_key"}).toString(), QStringLiteral("alt"));
        QCOMPARE(again.value({"snap", "gap"}).toDouble(), 1.0);
        QVERIFY(!again.contains({"snap", "mod_key"}));
    }

    void removesAKeyWithItsLine()
    {
        TomlDoc doc;
        QVERIFY(doc.parse(QStringLiteral("[snap]\ngap = 1.0 # x\nautostart = [\n \"a\",\n]\ncorners = true\n")));
        doc.remove({"snap", "autostart"});
        QCOMPARE(doc.text(), QStringLiteral("[snap]\ngap = 1.0 # x\ncorners = true\n"));
        doc.remove({"snap", "gap"});
        doc.remove({"snap", "missing"});
        QCOMPARE(doc.text(), QStringLiteral("[snap]\ncorners = true\n"));
    }

    void leavesArraysOfTablesAlone()
    {
        const QString text = QStringLiteral("[[outputs]]\nname = \"DP-1\"\nscale = 1.5\n[outputs.hot_corners]\ntop_left = \"quit\"\n\n[[window_rules]]\napp_id = \"x\"\n");
        TomlDoc doc;
        QVERIFY(doc.parse(text));
        QVERIFY(!doc.contains({"outputs", "name"}));
        QVERIFY(doc.keys({"outputs"}).isEmpty());
        doc.set({"snap", "gap"}, 3.0);
        QVERIFY(doc.text().startsWith(text));
        QCOMPARE(doc.value({"snap", "gap"}).toDouble(), 3.0);
    }

    void followsDottedAndInlineSpellings()
    {
        TomlDoc doc;
        QVERIFY(doc.parse(QStringLiteral("[input]\nkeyboard.layout = \"us\"\nmouse = { left_handed = true }\n")));
        QCOMPARE(doc.value({"input", "keyboard", "layout"}).toString(), QStringLiteral("us"));
        QCOMPARE(doc.value({"input", "mouse", "left_handed"}).toBool(), true);
        doc.set({"input", "keyboard", "variant"}, QStringLiteral("dvorak"));
        doc.set({"input", "mouse", "natural_scroll"}, true);
        doc.set({"input", "mouse", "left_handed"}, false);
        TomlDoc again;
        QVERIFY2(again.parse(doc.text()), qPrintable(doc.text()));
        QCOMPARE(again.value({"input", "keyboard", "variant"}).toString(), QStringLiteral("dvorak"));
        QCOMPARE(again.value({"input", "keyboard", "layout"}).toString(), QStringLiteral("us"));
        QCOMPARE(again.value({"input", "mouse", "natural_scroll"}).toBool(), true);
        QCOMPARE(again.value({"input", "mouse", "left_handed"}).toBool(), false);
        QVERIFY(!doc.text().contains(QLatin1String("[input.keyboard]")));
        doc.remove({"input", "mouse", "natural_scroll"});
        QVERIFY(again.parse(doc.text()));
        QVERIFY(!again.contains({"input", "mouse", "natural_scroll"}));
    }

    void encodesByKind()
    {
        QCOMPARE(TomlDoc::encode(3.0, TomlDoc::Kind::Int), QStringLiteral("3"));
        QCOMPARE(TomlDoc::encode(3, TomlDoc::Kind::Float), QStringLiteral("3.0"));
        QCOMPARE(TomlDoc::encode(0.1 + 0.2, TomlDoc::Kind::Float), QStringLiteral("0.3"));
        QCOMPARE(TomlDoc::encode(QStringLiteral("a\\b\n")), QStringLiteral("\"a\\\\b\\n\""));
        QCOMPARE(TomlDoc::encode(QStringList({"keys", "mouse"})), QStringLiteral("[\"keys\", \"mouse\"]"));
        QCOMPARE(TomlDoc::encodeKey(QStringLiteral("top_left")), QStringLiteral("top_left"));
        QCOMPARE(TomlDoc::encodeKey(QStringLiteral("mod+ñ")), QStringLiteral("\"mod+ñ\""));
        QCOMPARE(TomlDoc::encodeKey(QStringLiteral("ñ")), QStringLiteral("\"ñ\""));
    }

    void readsTheReferenceDefaults()
    {
        QFile file(QStringLiteral(REFERENCE_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QStringList lines;
        for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
            lines << (line.startsWith(QLatin1String("# ")) && !line.startsWith(QLatin1String("# #")) ? line.mid(2) : line);
        }
        TomlDoc doc;
        QString error;
        QVERIFY2(doc.parse(lines.join(QLatin1Char('\n')), &error), qPrintable(error));
        QCOMPARE(doc.value({"mod_key"}).toString(), QStringLiteral("super"));
        QCOMPARE(doc.value({"input", "keyboard", "repeat_delay"}).toInt(), 200);
        QCOMPARE(doc.value({"effects", "glass", "bevel"}).toInt(), 18);
        QCOMPARE(doc.value({"keybindings", "mod+q"}).toString(), QStringLiteral("close-window"));
        QVERIFY(doc.keys({"keybindings"}).size() > 40);
        QCOMPARE(doc.value({"mouse", "on-canvas", "wheel-scroll"}).toString(), QStringLiteral("zoom"));
    }
};

QTEST_APPLESS_MAIN(TomlDocTest)
#include "tst_tomldoc.moc"
