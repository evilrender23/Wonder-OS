#pragma once

#include "symbols.h"

#include <QList>
#include <QSharedPointer>
#include <QString>

struct Node;
using NodePtr = QSharedPointer<Node>;

/// One node of a formula's tree. What `kids` and `text` mean depends on `type`.
struct Node {
    enum Type {
        Row,     ///< kids: the atoms, left to right. A `{group}`.
        Symbol,  ///< text: the character(s); atom, big, upright.
        Text,    ///< text: words set upright (`\text`).
        Frac,    ///< kids: numerator, denominator. rule, left/right (binomials), mathStyle.
        Sqrt,    ///< kids: radicand, index (may be null).
        Script,  ///< kids: base, subscript, superscript (any may be null). stacked: above and below.
        Delim,   ///< kids: the body, with `middle` symbols in it. left, right.
        Matrix,  ///< cells: rows of cells. text: the environment. left, right, columns.
        Accent,  ///< text: the command (`hat`, `overline`…). kids: the body.
        Style,   ///< text: kind (`font`, `color`, `math`, `class`, `box`, `phantom`); arg. kids: the body.
        Space,   ///< width, in mu (18 to the em).
        Error,   ///< text: what could not be understood, shown as written.
    };
    Type type = Row;
    /// Where it comes from in the source: [start, end).
    int start = 0;
    int end = 0;
    QString text;
    QString arg;
    QString left;
    QString right;
    QString columns;
    QList<NodePtr> kids;
    QList<QList<NodePtr>> cells;
    Atom atom = Atom::Ord;
    qreal width = 0;
    /// Symbol: a big operator, larger in display style.
    bool big = false;
    /// Symbol: set upright (digits, function names).
    bool upright = false;
    /// Symbol: a delimiter of `\middle`, or one of a fixed size (`sized` > 0, in steps of \big).
    bool middle = false;
    int sized = 0;
    /// Symbol: scripts go above and below in display style. 1 always, 2 never (`\limits`, `\nolimits`).
    bool movable = false;
    int limits = 0;
    /// Script: over and under, whatever the style (`\overset`, `\underbrace{}_{}`).
    bool stacked = false;
    /// Frac: the bar is drawn. mathStyle: -1 as it comes, 0 display, 1 text.
    bool rule = true;
    int mathStyle = -1;
    /// Style: the body flows into the row around it (a declaration, a colour).
    bool flows = false;
    /// Row: nothing was written where something was expected; drawn as a box to fill.
    bool hole = false;
    /// Symbol: the command it was written with, without the backslash.
    QString command;
};

struct ParseError {
    int position = 0;
    int length = 1;
    QString message;
};

struct Parsed {
    NodePtr root;
    QList<ParseError> errors;
    bool ok() const { return errors.isEmpty(); }
};

namespace Latex {

/// Never fails: what cannot be understood becomes an Error node and an entry in `errors`.
Parsed parse(const QString &source);

/// A colour as LaTeX names it (`red`, `#ff0000`, `ForestGreen`), or an empty string.
QString colorName(const QString &name);

/// The position of the brace that pairs with the one at `position`, or -1.
int matchingBrace(const QString &source, int position);

}
