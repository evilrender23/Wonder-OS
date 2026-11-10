#pragma once

#include <QByteArray>
#include <QList>
#include <QStringList>

class Workbook;

/// Text with a separator between cells: CSV and TSV.
namespace Csv
{
/// Rows of fields; quotes may wrap a field, and inside them a doubled quote is one.
QList<QStringList> parse(const QString &text, QChar separator);
/// The separator a text seems to use: tab, semicolon or comma.
QChar detectSeparator(const QString &text);
/// The bytes as text: UTF-8 (with or without mark), UTF-16 by its mark, or Latin-1 / Windows-1252 when it is not valid UTF-8.
QString decode(const QByteArray &bytes, QString *encoding = nullptr);
QString write(const QList<QStringList> &rows, QChar separator);

/// Fills a sheet from the text. Numbers are read the way the separator hints:
/// with commas between fields, a dot is the decimal mark.
void load(Workbook &book, int sheet, const QByteArray &bytes, QChar separator = QChar());
/// A sheet as text, values as shown. `separator` «;» writes the Spanish way.
QByteArray save(Workbook &book, int sheet, QChar separator);
}
