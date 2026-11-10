#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

/// SQL as text: quoting, a tokenizer and what is built on it. No database here.
namespace Sql
{
/// An identifier between double quotes, whatever it holds.
QString ident(const QString &name);
/// A value as a literal, for dumps and generated queries.
QString literal(const QVariant &value);

/// SQLite's affinity for a declared type: INTEGER, REAL, TEXT, BLOB or NUMERIC.
QString affinity(const QString &declared);
/// What a column holds, for the editor of its cells: integer, real, text,
/// date, datetime, bool or blob.
QString kind(const QString &declared);
/// A declared type is a name with optional sizes, and nothing else.
bool validType(const QString &declared);
/// What may follow DEFAULT, made safe: numbers, keywords, a balanced
/// expression in parentheses and string literals pass; anything else is
/// taken as text and quoted. Empty stays empty.
QString defaultClause(const QString &typed);

struct Token {
    enum Type { Space, Comment, Keyword, Function, Identifier, String, Number, Operator, Parameter };
    Type type;
    int start;
    int length;
};
QList<Token> tokenize(const QString &sql);
bool isKeyword(const QString &word);
QStringList keywords();
QStringList functions();

/// The statements of a script, without the semicolons between them.
QStringList split(const QString &script);
/// The named parameters (`:name`, `@name`, `$name`) of a statement, in order
/// of appearance and without repeats.
QStringList parameters(const QString &sql);
/// The first word that is not a comment, in capitals.
QString verb(const QString &sql);
/// SELECT, WITH or VALUES: can be wrapped as a subquery to page through it.
bool selects(const QString &sql);
/// One statement with no trailing semicolon or comment.
QString trimmed(const QString &sql);

/// A criterion typed by a person for one column, as SQL with `?` for the
/// values it binds. `>5`, `<=x`, `<>x`, `a..b`, `null`, `no null`, `ana*`,
/// `=exacto`; plain text is «contains». Empty text gives an empty string.
QString criterion(const QString &expression, const QString &typed, QVariantList *binds);
/// The same with the values written in.
QString criterionInline(const QString &expression, const QString &typed);
/// One link of a chain of filters, as SQL with `?` for what it binds. The
/// operators: eq, ne, contains, excludes, starts, ends, gt, ge, lt, le, null,
/// notnull. Empty when the operator is not one of them.
QString condition(const QString &expression, const QString &op, const QVariant &value, QVariantList *binds);
/// A statement with the values of its `?` written in. Question marks inside
/// quoted identifiers and string literals are left alone.
QString inlined(const QString &sql, const QVariantList &binds);
/// The operators of condition(), with the words a person reads: [{id, label, unary}]
QVariantList operators();

/// What could be typed at `cursor`: {start, prefix, items: [{text, kind}]}.
/// `schema` maps each table to the names of its columns.
QVariantMap complete(const QString &text, int cursor, const QVariantMap &schema);

/// «fecha_alta» → «Fecha alta».
QString pretty(const QString &name);
}
