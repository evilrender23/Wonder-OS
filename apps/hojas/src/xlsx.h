#pragma once

#include <QByteArray>
#include <QString>

class Workbook;

/// Excel workbooks (.xlsx, SpreadsheetML in a ZIP).
namespace Xlsx
{
/// Fills the workbook from the bytes of a file. False, with a message in
/// Spanish, when it is not a workbook that can be read.
bool load(Workbook &book, const QByteArray &bytes, QString *error = nullptr);
/// The workbook as a file: values, formulas with their results, styles,
/// sizes, merges, frozen panes, filters, rules, validations and charts.
QByteArray save(Workbook &book);
}

/// OpenDocument spreadsheets (.ods). Reading only.
namespace Ods
{
bool load(Workbook &book, const QByteArray &bytes, QString *error = nullptr);
}
