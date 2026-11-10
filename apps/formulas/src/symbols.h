#pragma once

#include <QList>
#include <QString>
#include <QVariant>

/// The class of an atom, which decides the space around it (TeX's eight).
enum class Atom { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };

/// One command of the table: `\alpha`, `\sum`, `\leq`…
struct SymbolInfo {
    const char *command;
    char32_t code;
    Atom atom;
    /// Palette category: greek, operators, relations, arrows, sets, big, delimiters, misc.
    const char *category;
    /// Spanish names and keywords, for the search.
    const char *names;
    /// Big operators only: limits go above and below in display style.
    bool limits = false;
};

/// A function name set upright: `\sin`, `\lim`…
struct FunctionInfo {
    const char *command;
    const char *text;
    bool limits;
    const char *names;
};

/// A snippet of the palette: a fraction, a matrix… `|` marks where the cursor lands.
struct TemplateInfo {
    const char *id;
    const char *title;
    const char *latex;
    const char *category;
    const char *names;
};

namespace Symbols {

const SymbolInfo *find(const QString &command);
const SymbolInfo *byCode(char32_t code);
const FunctionInfo *function(const QString &command);
const QList<SymbolInfo> &all();
const QList<FunctionInfo> &functions();
const QList<TemplateInfo> &templates();

/// Lower case, without accents: what the searches compare.
QString fold(const QString &text);

/// The palette: [{id, title, items: [{latex, insert, title, names, glyph}]}], filtered by `query`.
QVariantList palette(const QString &query);
/// What may follow a backslash: [{command, insert, title, glyph}] for commands starting with `prefix`.
QVariantList completions(const QString &prefix, int limit = 8);
/// Every command the parser knows, for the highlighter.
bool known(const QString &command);

}
