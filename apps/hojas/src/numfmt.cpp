#include "numfmt.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

#include <cmath>
#include <limits>

QString Value::errorText(Err error)
{
    switch (error) {
    case Err::Div0:
        return QStringLiteral("#DIV/0!");
    case Err::Value:
        return QStringLiteral("#VALUE!");
    case Err::Ref:
        return QStringLiteral("#REF!");
    case Err::Name:
        return QStringLiteral("#NAME?");
    case Err::NA:
        return QStringLiteral("#N/A");
    case Err::Num:
        return QStringLiteral("#NUM!");
    case Err::Null:
        return QStringLiteral("#NULL!");
    case Err::Cycle:
        return QStringLiteral("#CIRC!");
    case Err::None:
        break;
    }
    return {};
}

Err Value::errorFromText(const QString &text)
{
    static const QHash<QString, Err> known = {
        {QStringLiteral("#DIV/0!"), Err::Div0}, {QStringLiteral("#VALUE!"), Err::Value}, {QStringLiteral("#REF!"), Err::Ref},
        {QStringLiteral("#NAME?"), Err::Name},  {QStringLiteral("#N/A"), Err::NA},       {QStringLiteral("#NUM!"), Err::Num},
        {QStringLiteral("#NULL!"), Err::Null},  {QStringLiteral("#CIRC!"), Err::Cycle},  {QStringLiteral("#¡DIV/0!"), Err::Div0},
        {QStringLiteral("#¡VALOR!"), Err::Value}, {QStringLiteral("#¡REF!"), Err::Ref},  {QStringLiteral("#¿NOMBRE?"), Err::Name},
        {QStringLiteral("#¡NUM!"), Err::Num},   {QStringLiteral("#¡NULO!"), Err::Null},
    };
    return known.value(text.trimmed().toUpper(), Err::None);
}

namespace
{
const QChar decimalMark = QLatin1Char(',');
const QChar groupMark = QLatin1Char('.');

const char *const monthNames[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
const char *const dayNames[] = {"lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"};

enum class Tok { Literal, Digit, Point, Exponent, Percent, Text, General, Year, Month, Day, Hour, Minute, Second, AmPm, Elapsed, Fraction };

struct Token {
    Tok kind;
    QString text;
};

struct Section {
    QList<Token> tokens;
    QString color;
    bool isDate = false;
    bool hasDigits = false;
    bool group = false;
    bool general = false;
    bool text = false;
    int scale = 0; // trailing commas: thousands dropped
    int percents = 0;
};

QString colorOf(const QString &name)
{
    static const QHash<QString, QString> colors = {
        {QStringLiteral("black"), QStringLiteral("#000000")},   {QStringLiteral("blue"), QStringLiteral("#0a60ff")},
        {QStringLiteral("cyan"), QStringLiteral("#00a6c0")},    {QStringLiteral("green"), QStringLiteral("#1f9d55")},
        {QStringLiteral("magenta"), QStringLiteral("#c2189a")}, {QStringLiteral("red"), QStringLiteral("#e0352b")},
        {QStringLiteral("white"), QStringLiteral("#ffffff")},   {QStringLiteral("yellow"), QStringLiteral("#c79a00")},
        {QStringLiteral("negro"), QStringLiteral("#000000")},   {QStringLiteral("azul"), QStringLiteral("#0a60ff")},
        {QStringLiteral("verde"), QStringLiteral("#1f9d55")},   {QStringLiteral("rojo"), QStringLiteral("#e0352b")},
    };
    return colors.value(name.toLower());
}

QStringList splitSections(const QString &code)
{
    QStringList out;
    QString current;
    bool quoted = false;
    int bracket = 0;
    for (int i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (quoted) {
            current += c;
            quoted = c != QLatin1Char('"');
        } else if (c == QLatin1Char('"')) {
            quoted = true;
            current += c;
        } else if (c == QLatin1Char('\\') && i + 1 < code.size()) {
            current += c;
            current += code.at(++i);
        } else if (c == QLatin1Char('[')) {
            ++bracket;
            current += c;
        } else if (c == QLatin1Char(']')) {
            bracket = qMax(0, bracket - 1);
            current += c;
        } else if (c == QLatin1Char(';') && bracket == 0) {
            out << current;
            current.clear();
        } else {
            current += c;
        }
    }
    out << current;
    return out;
}

Section parseSection(const QString &code)
{
    Section s;
    auto literal = [&s](const QString &text) {
        if (!s.tokens.isEmpty() && s.tokens.last().kind == Tok::Literal) {
            s.tokens.last().text += text;
        } else {
            s.tokens.append({Tok::Literal, text});
        }
    };
    const int n = code.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = code.at(i);
        const QChar low = c.toLower();
        if (c == QLatin1Char('"')) {
            const int end = code.indexOf(QLatin1Char('"'), i + 1);
            literal(code.mid(i + 1, (end < 0 ? n : end) - i - 1));
            i = end < 0 ? n : end;
        } else if (c == QLatin1Char('\\')) {
            if (i + 1 < n) {
                literal(code.mid(++i, 1));
            }
        } else if (c == QLatin1Char('_')) {
            // The width of the next character: a space does.
            if (i + 1 < n) {
                ++i;
                literal(QStringLiteral(" "));
            }
        } else if (c == QLatin1Char('*')) {
            // Fill with the next character: nothing to fill here.
            ++i;
        } else if (c == QLatin1Char('[')) {
            const int end = code.indexOf(QLatin1Char(']'), i);
            const QString inside = code.mid(i + 1, (end < 0 ? n : end) - i - 1);
            i = end < 0 ? n : end;
            const QString lowInside = inside.toLower();
            if (lowInside == QLatin1String("h") || lowInside == QLatin1String("hh") || lowInside == QLatin1String("m") || lowInside == QLatin1String("mm")
                || lowInside == QLatin1String("s") || lowInside == QLatin1String("ss")) {
                s.tokens.append({Tok::Elapsed, lowInside});
                s.isDate = true;
            } else if (inside.startsWith(QLatin1Char('$'))) {
                // [$€-C0A]: a currency sign and a locale.
                const int dash = inside.indexOf(QLatin1Char('-'));
                const QString sign = inside.mid(1, (dash < 0 ? inside.size() : dash) - 1);
                if (!sign.isEmpty()) {
                    literal(sign);
                }
            } else if (!colorOf(inside).isEmpty()) {
                s.color = colorOf(inside);
            }
        } else if (c == QLatin1Char('0') || c == QLatin1Char('#') || c == QLatin1Char('?')) {
            s.tokens.append({Tok::Digit, QString(c)});
            s.hasDigits = true;
        } else if (c == QLatin1Char('.')) {
            s.tokens.append({Tok::Point, QString()});
        } else if (c == QLatin1Char(',')) {
            // Between digits it groups thousands; after the last digit it drops them.
            bool digitAfter = false;
            for (int j = i + 1; j < n; ++j) {
                const QChar d = code.at(j);
                if (d == QLatin1Char('0') || d == QLatin1Char('#') || d == QLatin1Char('?')) {
                    digitAfter = true;
                    break;
                }
                if (d != QLatin1Char(',')) {
                    break;
                }
            }
            if (!s.hasDigits) {
                literal(QStringLiteral(","));
            } else if (digitAfter) {
                s.group = true;
            } else {
                ++s.scale;
            }
        } else if (c == QLatin1Char('%')) {
            s.tokens.append({Tok::Percent, QString()});
            ++s.percents;
        } else if (c == QLatin1Char('@')) {
            s.tokens.append({Tok::Text, QString()});
            s.text = true;
        } else if (low == QLatin1Char('e') && i + 1 < n && (code.at(i + 1) == QLatin1Char('+') || code.at(i + 1) == QLatin1Char('-')) && s.hasDigits) {
            s.tokens.append({Tok::Exponent, QString(code.at(i + 1))});
            ++i;
        } else if (code.mid(i, 7).compare(QLatin1String("General"), Qt::CaseInsensitive) == 0) {
            s.tokens.append({Tok::General, QString()});
            s.general = true;
            i += 6;
        } else if (code.mid(i, 5).compare(QLatin1String("AM/PM"), Qt::CaseInsensitive) == 0) {
            s.tokens.append({Tok::AmPm, QStringLiteral("AM/PM")});
            s.isDate = true;
            i += 4;
        } else if (code.mid(i, 3).compare(QLatin1String("A/P"), Qt::CaseInsensitive) == 0) {
            s.tokens.append({Tok::AmPm, QStringLiteral("A/P")});
            s.isDate = true;
            i += 2;
        } else if (low == QLatin1Char('y') || low == QLatin1Char('m') || low == QLatin1Char('d') || low == QLatin1Char('h') || low == QLatin1Char('s')) {
            int run = 1;
            while (i + run < n && code.at(i + run).toLower() == low) {
                ++run;
            }
            const Tok kind = low == QLatin1Char('y') ? Tok::Year : low == QLatin1Char('m') ? Tok::Month : low == QLatin1Char('d') ? Tok::Day
                : low == QLatin1Char('h') ? Tok::Hour : Tok::Second;
            s.tokens.append({kind, QString(run, low)});
            s.isDate = true;
            i += run - 1;
        } else if (c == QLatin1Char('/') && s.hasDigits && !s.isDate) {
            s.tokens.append({Tok::Fraction, QString()});
        } else {
            literal(QString(c));
        }
    }
    if (s.isDate) {
        // «m» is minutes right after hours or right before seconds, months otherwise.
        QList<int> parts;
        for (int i = 0; i < s.tokens.size(); ++i) {
            const Tok kind = s.tokens.at(i).kind;
            if (kind == Tok::Year || kind == Tok::Month || kind == Tok::Day || kind == Tok::Hour || kind == Tok::Second || kind == Tok::Elapsed) {
                parts << i;
            }
        }
        for (int p = 0; p < parts.size(); ++p) {
            Token &token = s.tokens[parts.at(p)];
            if (token.kind != Tok::Month || token.text.size() > 2) {
                continue;
            }
            const bool afterHour = p > 0 && (s.tokens.at(parts.at(p - 1)).kind == Tok::Hour
                || (s.tokens.at(parts.at(p - 1)).kind == Tok::Elapsed && s.tokens.at(parts.at(p - 1)).text.startsWith(QLatin1Char('h'))));
            const bool beforeSecond = p + 1 < parts.size() && s.tokens.at(parts.at(p + 1)).kind == Tok::Second;
            if (afterHour || beforeSecond) {
                token.kind = Tok::Minute;
            }
        }
    }
    return s;
}

const QList<Section> &sections(const QString &code)
{
    static QHash<QString, QList<Section>> cache;
    auto it = cache.constFind(code);
    if (it == cache.constEnd()) {
        QList<Section> parsed;
        for (const QString &part : splitSections(code)) {
            parsed << parseSection(part);
        }
        if (cache.size() > 2000) {
            cache.clear();
        }
        it = cache.insert(code, parsed);
    }
    return it.value();
}

QString grouped(const QString &digits)
{
    QString out;
    const int n = digits.size();
    for (int i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) {
            out += groupMark;
        }
        out += digits.at(i);
    }
    return out;
}

/// The digits of a number rounded to `places` decimals, without sign.
void digitsOf(double value, int places, QString *whole, QString *fraction)
{
    // 1.005 is a hair under 1.005 in binary: lean on it so it rounds as written.
    const double nudged = value + std::abs(value) * 4e-15;
    const QString text = QString::number(std::abs(nudged), 'f', qBound(0, places, 30));
    const int dot = text.indexOf(QLatin1Char('.'));
    *whole = dot < 0 ? text : text.left(dot);
    *fraction = dot < 0 ? QString() : text.mid(dot + 1);
}

QString formatDate(const Section &s, double value)
{
    const bool negative = value < 0;
    double whole = std::floor(value);
    // To the second (or the thousandth), so 0.999999 does not read 23:59:59.
    double seconds = std::round((value - whole) * 86400.0);
    if (seconds >= 86400.0) {
        seconds -= 86400.0;
        whole += 1;
    }
    const QDate date = NumFmt::date(whole);
    const int secondOfDay = int(seconds);
    const int hour = secondOfDay / 3600;
    const int minute = secondOfDay / 60 % 60;
    const int second = secondOfDay % 60;
    bool twelve = false;
    for (const Token &token : s.tokens) {
        twelve = twelve || token.kind == Tok::AmPm;
    }
    if (negative || !date.isValid()) {
        return QStringLiteral("#####");
    }
    QString out;
    auto padded = [](qint64 number, int width) {
        return QStringLiteral("%1").arg(number, width, 10, QLatin1Char('0'));
    };
    for (const Token &token : s.tokens) {
        const int width = token.text.size();
        switch (token.kind) {
        case Tok::Literal:
            out += token.text;
            break;
        case Tok::Year:
            out += width <= 2 ? padded(date.year() % 100, 2) : padded(date.year(), 4);
            break;
        case Tok::Month:
            if (width == 1) {
                out += QString::number(date.month());
            } else if (width == 2) {
                out += padded(date.month(), 2);
            } else if (width == 3) {
                out += QString::fromUtf8(monthNames[date.month() - 1]).left(3);
            } else if (width == 5) {
                out += QString::fromUtf8(monthNames[date.month() - 1]).left(1).toUpper();
            } else {
                out += QString::fromUtf8(monthNames[date.month() - 1]);
            }
            break;
        case Tok::Day:
            if (width == 1) {
                out += QString::number(date.day());
            } else if (width == 2) {
                out += padded(date.day(), 2);
            } else if (width == 3) {
                out += QString::fromUtf8(dayNames[date.dayOfWeek() - 1]).left(3);
            } else {
                out += QString::fromUtf8(dayNames[date.dayOfWeek() - 1]);
            }
            break;
        case Tok::Hour: {
            const int shown = twelve ? (hour % 12 == 0 ? 12 : hour % 12) : hour;
            out += width == 1 ? QString::number(shown) : padded(shown, 2);
            break;
        }
        case Tok::Minute:
            out += width == 1 ? QString::number(minute) : padded(minute, 2);
            break;
        case Tok::Second:
            out += width == 1 ? QString::number(second) : padded(second, 2);
            break;
        case Tok::AmPm:
            out += token.text == QLatin1String("A/P") ? (hour < 12 ? QStringLiteral("a") : QStringLiteral("p")) : (hour < 12 ? QStringLiteral("AM") : QStringLiteral("PM"));
            break;
        case Tok::Elapsed: {
            const double total = whole * 86400.0 + seconds;
            const qint64 amount = token.text.startsWith(QLatin1Char('h')) ? qint64(total / 3600) : token.text.startsWith(QLatin1Char('m')) ? qint64(total / 60) : qint64(total);
            out += padded(amount, width);
            break;
        }
        default:
            break;
        }
    }
    return out;
}

QString formatNumber(const Section &s, double value, bool addSign)
{
    double shown = std::abs(value);
    for (int i = 0; i < s.percents; ++i) {
        shown *= 100.0;
    }
    for (int i = 0; i < s.scale; ++i) {
        shown /= 1000.0;
    }
    // Where the digit places are.
    int point = -1;
    int exponent = -1;
    int fractionBar = -1;
    QList<int> wholePlaces, decimalPlaces, exponentPlaces;
    for (int i = 0; i < s.tokens.size(); ++i) {
        const Tok kind = s.tokens.at(i).kind;
        if (kind == Tok::Point && point < 0 && exponent < 0) {
            point = i;
        } else if (kind == Tok::Exponent && exponent < 0) {
            exponent = i;
        } else if (kind == Tok::Fraction && fractionBar < 0) {
            fractionBar = i;
        } else if (kind == Tok::Digit) {
            (exponent >= 0 ? exponentPlaces : point >= 0 ? decimalPlaces : wholePlaces) << i;
        }
    }
    if (fractionBar >= 0) {
        // Fractions are rare: show the plain number rather than a wrong one.
        return (addSign && value < 0 ? QStringLiteral("-") : QString()) + NumFmt::general(shown);
    }
    int power = 0;
    if (exponent >= 0 && shown != 0) {
        const int span = qMax(1, int(wholePlaces.size()));
        power = int(std::floor(std::log10(shown)));
        // «##0.0E+0» moves in threes: the exponent is a multiple of the whole places.
        power = int(std::floor(double(power) / span)) * span;
        shown /= std::pow(10.0, power);
        QString w, f;
        digitsOf(shown, decimalPlaces.size(), &w, &f);
        if (w.size() > span) {
            power += span;
            shown /= std::pow(10.0, span);
        }
    }
    QString whole, fraction;
    digitsOf(shown, decimalPlaces.size(), &whole, &fraction);
    if (whole == QLatin1String("0")) {
        whole.clear();
    }
    const bool zero = whole.isEmpty() && fraction.count(QLatin1Char('0')) == fraction.size();

    QHash<int, QString> filled;
    // Whole digits fill from the right; the leftmost place takes what is left.
    int left = whole.size();
    for (int p = wholePlaces.size() - 1; p >= 0; --p) {
        const int at = wholePlaces.at(p);
        const QChar place = s.tokens.at(at).text.at(0);
        QString text;
        if (p == 0 && left > 0) {
            text = whole.left(left);
            left = 0;
        } else if (left > 0) {
            text = whole.mid(--left, 1);
        } else {
            text = place == QLatin1Char('0') ? QStringLiteral("0") : place == QLatin1Char('?') ? QStringLiteral(" ") : QString();
        }
        filled.insert(at, text);
    }
    if (s.group && !wholePlaces.isEmpty()) {
        // Group the run of digits as a whole, then hand it back to the places.
        QString all;
        for (int at : wholePlaces) {
            all += filled.value(at);
        }
        const QString digits = all.trimmed();
        const QString pad = all.left(all.size() - digits.size());
        for (int at : wholePlaces) {
            filled.insert(at, QString());
        }
        filled.insert(wholePlaces.last(), pad + grouped(digits));
    }
    // Decimals fill from the left; trailing zeros go where the place is «#».
    int lastKept = -1;
    for (int p = 0; p < decimalPlaces.size(); ++p) {
        const QChar place = s.tokens.at(decimalPlaces.at(p)).text.at(0);
        if (place == QLatin1Char('0') || fraction.at(p) != QLatin1Char('0')) {
            lastKept = p;
        }
    }
    for (int p = 0; p < decimalPlaces.size(); ++p) {
        const QChar place = s.tokens.at(decimalPlaces.at(p)).text.at(0);
        QString text;
        if (p <= lastKept) {
            text = fraction.mid(p, 1);
        } else if (place == QLatin1Char('?')) {
            text = QStringLiteral(" ");
        }
        filled.insert(decimalPlaces.at(p), text);
    }
    if (!exponentPlaces.isEmpty()) {
        QString digits = QString::number(std::abs(power));
        int leftE = digits.size();
        for (int p = exponentPlaces.size() - 1; p >= 0; --p) {
            const QChar place = s.tokens.at(exponentPlaces.at(p)).text.at(0);
            QString text;
            if (p == 0 && leftE > 0) {
                text = digits.left(leftE);
                leftE = 0;
            } else if (leftE > 0) {
                text = digits.mid(--leftE, 1);
            } else if (place == QLatin1Char('0')) {
                text = QStringLiteral("0");
            }
            filled.insert(exponentPlaces.at(p), text);
        }
    }

    QString out;
    for (int i = 0; i < s.tokens.size(); ++i) {
        const Token &token = s.tokens.at(i);
        switch (token.kind) {
        case Tok::Literal:
            out += token.text;
            break;
        case Tok::Digit:
            out += filled.value(i);
            break;
        case Tok::Point:
            if (i == point) {
                // No comma hanging at the end of «#.##» when there are no decimals.
                if (lastKept >= 0 || decimalPlaces.isEmpty()) {
                    out += decimalMark;
                }
            } else {
                out += decimalMark;
            }
            break;
        case Tok::Percent:
            out += QLatin1Char('%');
            break;
        case Tok::Exponent:
            out += QLatin1Char('E');
            out += power < 0 ? QStringLiteral("-") : (token.text == QLatin1String("+") ? QStringLiteral("+") : QString());
            break;
        case Tok::General:
            out += NumFmt::general(shown);
            break;
        default:
            break;
        }
    }
    if (addSign && value < 0 && !zero) {
        out.prepend(QLatin1Char('-'));
    }
    return out;
}

QString formatText(const Section &s, const QString &text)
{
    QString out;
    for (const Token &token : s.tokens) {
        if (token.kind == Tok::Literal) {
            out += token.text;
        } else if (token.kind == Tok::Text) {
            out += text;
        }
    }
    return out;
}

bool isGeneral(const QString &code)
{
    return code.isEmpty() || code.compare(QLatin1String("General"), Qt::CaseInsensitive) == 0;
}
}

// --- Dates --------------------------------------------------------------------

static const QDate epoch(1899, 12, 30);

double NumFmt::serial(const QDate &date)
{
    return date.isValid() ? double(epoch.daysTo(date)) : 0;
}

double NumFmt::serial(const QDateTime &moment)
{
    return serial(moment.date()) + moment.time().msecsSinceStartOfDay() / 86400000.0;
}

QDate NumFmt::date(double serial)
{
    if (!(serial > -700000 && serial < 2958466)) {
        return {};
    }
    return epoch.addDays(qint64(std::floor(serial)));
}

QDateTime NumFmt::dateTime(double serial)
{
    const QDate day = date(serial);
    const int ms = int(std::round((serial - std::floor(serial)) * 86400000.0));
    return QDateTime(day, QTime::fromMSecsSinceStartOfDay(qMin(ms, 86399999)), QTimeZone::UTC);
}

// --- Out ----------------------------------------------------------------------

QString NumFmt::general(double number)
{
    if (std::isnan(number) || std::isinf(number)) {
        return QStringLiteral("#NUM!");
    }
    if (number == 0) {
        return QStringLiteral("0");
    }
    QString text = QString::number(number, 'g', 10);
    const int e = text.indexOf(QLatin1Char('e'));
    if (e >= 0) {
        QString mantissa = text.left(e);
        QString power = text.mid(e + 1);
        const bool minus = power.startsWith(QLatin1Char('-'));
        power.remove(QLatin1Char('+')).remove(QLatin1Char('-'));
        while (power.size() < 2) {
            power.prepend(QLatin1Char('0'));
        }
        text = mantissa + QLatin1Char('E') + (minus ? QLatin1Char('-') : QLatin1Char('+')) + power;
    }
    text.replace(QLatin1Char('.'), decimalMark);
    return text;
}

QString NumFmt::format(const Value &value, const QString &code, QString *color)
{
    switch (value.type) {
    case Value::Empty:
        return {};
    case Value::Error:
        return Value::errorText(value.err);
    case Value::Bool:
        return value.truth() ? QStringLiteral("VERDADERO") : QStringLiteral("FALSO");
    case Value::Text: {
        if (isGeneral(code)) {
            return value.text;
        }
        const QList<Section> &all = sections(code);
        if (all.size() >= 4) {
            return formatText(all.at(3), value.text);
        }
        if (all.size() == 1 && all.first().text) {
            return formatText(all.first(), value.text);
        }
        return value.text;
    }
    case Value::Number:
        break;
    }
    if (isGeneral(code)) {
        return general(value.num);
    }
    const QList<Section> &all = sections(code);
    const double number = value.num;
    int which = 0;
    bool addSign = true;
    if (all.size() >= 3 && number == 0) {
        which = 2;
    } else if (all.size() >= 2 && number < 0) {
        which = 1;
        addSign = false;
    }
    const Section &s = all.at(which);
    if (color && !s.color.isEmpty()) {
        *color = s.color;
    }
    if (s.isDate) {
        return formatDate(s, number);
    }
    if (s.text && !s.hasDigits) {
        return formatText(s, general(number));
    }
    if (s.general && !s.hasDigits) {
        QString out;
        for (const Token &token : s.tokens) {
            out += token.kind == Tok::Literal ? token.text : token.kind == Tok::General ? general(addSign ? number : std::abs(number)) : QString();
        }
        return out;
    }
    if (!s.hasDigits) {
        // Only literals: «"sí";"no"».
        QString out;
        for (const Token &token : s.tokens) {
            out += token.kind == Tok::Literal ? token.text : token.kind == Tok::Percent ? QStringLiteral("%") : QString();
        }
        return out;
    }
    return formatNumber(s, number, addSign);
}

QString NumFmt::editText(const Value &value, const QString &code)
{
    switch (value.type) {
    case Value::Empty:
        return {};
    case Value::Text:
        return value.text;
    case Value::Bool:
        return value.truth() ? QStringLiteral("VERDADERO") : QStringLiteral("FALSO");
    case Value::Error:
        return Value::errorText(value.err);
    case Value::Number:
        break;
    }
    auto plain = [](double number) {
        QString text = QString::number(number, 'g', 15);
        if (text.contains(QLatin1Char('e'))) {
            return general(number);
        }
        return text.replace(QLatin1Char('.'), decimalMark);
    };
    if (isDate(code)) {
        const double day = std::floor(value.num);
        const bool hasTime = std::abs(value.num - day) > 1e-9;
        if (isTime(code) && day == 0) {
            return format(value, QStringLiteral("hh:mm:ss"));
        }
        return format(value, hasTime ? QStringLiteral("dd/mm/yyyy hh:mm:ss") : QStringLiteral("dd/mm/yyyy"));
    }
    if (isPercent(code)) {
        return plain(std::round(value.num * 1e14) / 1e12) + QLatin1Char('%');
    }
    return plain(value.num);
}

// --- What a code is -------------------------------------------------------------

bool NumFmt::isDate(const QString &code)
{
    if (isGeneral(code)) {
        return false;
    }
    const QList<Section> &all = sections(code);
    return !all.isEmpty() && all.first().isDate;
}

bool NumFmt::isTime(const QString &code)
{
    if (!isDate(code)) {
        return false;
    }
    for (const Token &token : sections(code).first().tokens) {
        if (token.kind == Tok::Year || token.kind == Tok::Month || token.kind == Tok::Day) {
            return false;
        }
    }
    return true;
}

bool NumFmt::isPercent(const QString &code)
{
    return !isGeneral(code) && sections(code).first().percents > 0 && !sections(code).first().isDate;
}

bool NumFmt::isText(const QString &code)
{
    return code == QLatin1String("@");
}

QString NumFmt::builtin(int id)
{
    switch (id) {
    case 0:
        return QStringLiteral("General");
    case 1:
        return QStringLiteral("0");
    case 2:
        return QStringLiteral("0.00");
    case 3:
        return QStringLiteral("#,##0");
    case 4:
        return QStringLiteral("#,##0.00");
    case 9:
        return QStringLiteral("0%");
    case 10:
        return QStringLiteral("0.00%");
    case 11:
        return QStringLiteral("0.00E+00");
    case 12:
        return QStringLiteral("# ?/?");
    case 13:
        return QStringLiteral("# ??/??");
    case 14:
        return QStringLiteral("dd/mm/yyyy");
    case 15:
        return QStringLiteral("d-mmm-yy");
    case 16:
        return QStringLiteral("d-mmm");
    case 17:
        return QStringLiteral("mmm-yy");
    case 18:
        return QStringLiteral("h:mm AM/PM");
    case 19:
        return QStringLiteral("h:mm:ss AM/PM");
    case 20:
        return QStringLiteral("h:mm");
    case 21:
        return QStringLiteral("h:mm:ss");
    case 22:
        return QStringLiteral("dd/mm/yyyy h:mm");
    case 37:
        return QStringLiteral("#,##0;-#,##0");
    case 38:
        return QStringLiteral("#,##0;[Red]-#,##0");
    case 39:
        return QStringLiteral("#,##0.00;-#,##0.00");
    case 40:
        return QStringLiteral("#,##0.00;[Red]-#,##0.00");
    case 45:
        return QStringLiteral("mm:ss");
    case 46:
        return QStringLiteral("[h]:mm:ss");
    case 47:
        return QStringLiteral("mm:ss.0");
    case 48:
        return QStringLiteral("##0.0E+0");
    case 49:
        return QStringLiteral("@");
    default:
        return {};
    }
}

int NumFmt::builtinId(const QString &code)
{
    if (isGeneral(code)) {
        return 0;
    }
    static const int ids[] = {1, 2, 3, 4, 9, 10, 11, 14, 20, 21, 22, 46, 49};
    for (int id : ids) {
        if (builtin(id) == code) {
            return id;
        }
    }
    return -1;
}

int NumFmt::decimals(const QString &code)
{
    if (isGeneral(code) || isDate(code)) {
        return 0;
    }
    const Section &s = sections(code).first();
    int count = 0;
    bool after = false;
    for (const Token &token : s.tokens) {
        if (token.kind == Tok::Point) {
            after = true;
        } else if (token.kind == Tok::Exponent) {
            break;
        } else if (token.kind == Tok::Digit && after) {
            ++count;
        }
    }
    return count;
}

QString NumFmt::withDecimals(const QString &code, int delta)
{
    if (isDate(code) || isText(code)) {
        return code;
    }
    if (isGeneral(code)) {
        return delta > 0 ? QStringLiteral("0.0") : QStringLiteral("0");
    }
    // Every section gets the same treatment, in the text of the code itself.
    QStringList out;
    for (const QString &part : splitSections(code)) {
        QString section = part;
        // The last digit place outside quotes and brackets, and the point if any.
        int lastDigit = -1;
        int point = -1;
        int exponent = -1;
        bool quoted = false;
        int bracket = 0;
        for (int i = 0; i < section.size(); ++i) {
            const QChar c = section.at(i);
            if (quoted) {
                quoted = c != QLatin1Char('"');
            } else if (c == QLatin1Char('"')) {
                quoted = true;
            } else if (c == QLatin1Char('\\')) {
                ++i;
            } else if (c == QLatin1Char('[')) {
                ++bracket;
            } else if (c == QLatin1Char(']')) {
                --bracket;
            } else if (bracket > 0) {
                continue;
            } else if ((c == QLatin1Char('E') || c == QLatin1Char('e')) && exponent < 0 && lastDigit >= 0) {
                exponent = i;
            } else if (exponent >= 0) {
                continue;
            } else if (c == QLatin1Char('.') && point < 0) {
                point = i;
            } else if (c == QLatin1Char('0') || c == QLatin1Char('#') || c == QLatin1Char('?')) {
                lastDigit = i;
            }
        }
        if (lastDigit < 0) {
            out << section;
            continue;
        }
        if (delta > 0) {
            if (point < 0) {
                section.insert(lastDigit + 1, QStringLiteral(".0"));
            } else {
                section.insert(qMax(lastDigit, point) + 1, QLatin1Char('0'));
            }
        } else if (point >= 0 && lastDigit > point) {
            section.remove(lastDigit, 1);
            if (lastDigit - 1 == point) {
                section.remove(point, 1);
            }
        }
        out << section;
    }
    return out.join(QLatin1Char(';'));
}

QString NumFmt::family(const QString &code)
{
    if (isGeneral(code)) {
        return QStringLiteral("general");
    }
    if (isText(code)) {
        return QStringLiteral("text");
    }
    if (isDate(code)) {
        return isTime(code) ? QStringLiteral("time") : QStringLiteral("date");
    }
    if (isPercent(code)) {
        return QStringLiteral("percent");
    }
    const Section &s = sections(code).first();
    for (const Token &token : s.tokens) {
        if (token.kind == Tok::Exponent) {
            return QStringLiteral("scientific");
        }
    }
    for (const Token &token : s.tokens) {
        if (token.kind == Tok::Literal && (token.text.contains(QChar(0x20ac)) || token.text.contains(QLatin1Char('$')) || token.text.contains(QChar(0xa3)))) {
            return QStringLiteral("currency");
        }
    }
    return QStringLiteral("number");
}

QString NumFmt::familyCode(const QString &family, int places)
{
    const QString zeros = places > 0 ? QLatin1Char('.') + QString(places, QLatin1Char('0')) : QString();
    if (family == QLatin1String("number")) {
        return QStringLiteral("#,##0") + zeros;
    }
    if (family == QLatin1String("currency")) {
        return QStringLiteral("#,##0") + zeros + QStringLiteral("\\ \"€\"");
    }
    if (family == QLatin1String("percent")) {
        return QStringLiteral("0") + zeros + QLatin1Char('%');
    }
    if (family == QLatin1String("scientific")) {
        return QStringLiteral("0") + (places > 0 ? zeros : QStringLiteral(".00")) + QStringLiteral("E+00");
    }
    if (family == QLatin1String("date")) {
        return QStringLiteral("dd/mm/yyyy");
    }
    if (family == QLatin1String("time")) {
        return QStringLiteral("hh:mm:ss");
    }
    if (family == QLatin1String("text")) {
        return QStringLiteral("@");
    }
    return {};
}

// --- In -----------------------------------------------------------------------

double NumFmt::parseNumber(const QString &typed)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    QString text = typed.trimmed();
    if (text.isEmpty()) {
        return nan;
    }
    static const QRegularExpression shape(QStringLiteral("^[+-]?(\\d[\\d.,\\x{00a0} ]*|[.,]\\d+)([eE][+-]?\\d+)?$"));
    if (!shape.match(text).hasMatch()) {
        return nan;
    }
    text.remove(QChar(0xa0)).remove(QLatin1Char(' '));
    QString power;
    const int e = text.indexOf(QLatin1Char('e'), 0, Qt::CaseInsensitive);
    if (e >= 0) {
        power = text.mid(e);
        text = text.left(e);
    }
    const int dots = text.count(QLatin1Char('.'));
    const int commas = text.count(QLatin1Char(','));
    static const QRegularExpression dotGroups(QStringLiteral("^[+-]?\\d{1,3}(\\.\\d{3})+$"));
    static const QRegularExpression commaGroups(QStringLiteral("^[+-]?\\d{1,3}(,\\d{3})+$"));
    if (dots > 0 && commas > 0) {
        // The one that comes last is the decimal mark.
        if (text.lastIndexOf(QLatin1Char(',')) > text.lastIndexOf(QLatin1Char('.'))) {
            if (commas > 1) {
                return nan;
            }
            text.remove(QLatin1Char('.')).replace(QLatin1Char(','), QLatin1Char('.'));
        } else {
            if (dots > 1) {
                return nan;
            }
            text.remove(QLatin1Char(','));
        }
    } else if (commas == 1) {
        text.replace(QLatin1Char(','), QLatin1Char('.'));
    } else if (commas > 1) {
        if (!commaGroups.match(text).hasMatch()) {
            return nan;
        }
        text.remove(QLatin1Char(','));
    } else if (dots > 1 || (dots == 1 && dotGroups.match(text).hasMatch())) {
        // 1.234.567, and 1.234 too: a dot before three digits groups thousands.
        if (!dotGroups.match(text).hasMatch()) {
            return nan;
        }
        text.remove(QLatin1Char('.'));
    }
    bool ok = false;
    const double number = (text + power).toDouble(&ok);
    return ok ? number : nan;
}

namespace
{
bool parseTime(const QString &text, double *fraction, bool *withSeconds)
{
    static const QRegularExpression shape(QStringLiteral("^(\\d{1,2}):(\\d{2})(?::(\\d{2}))?\\s*(am|pm|a\\.?\\s?m\\.?|p\\.?\\s?m\\.?)?$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = shape.match(text.trimmed());
    if (!m.hasMatch()) {
        return false;
    }
    int hour = m.captured(1).toInt();
    const int minute = m.captured(2).toInt();
    const int second = m.captured(3).toInt();
    const QString half = m.captured(4).toLower().remove(QLatin1Char('.')).remove(QLatin1Char(' '));
    if (!half.isEmpty()) {
        if (hour < 1 || hour > 12) {
            return false;
        }
        hour = hour % 12 + (half == QLatin1String("pm") ? 12 : 0);
    }
    if (hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    *fraction = (hour * 3600 + minute * 60 + second) / 86400.0;
    *withSeconds = !m.captured(3).isEmpty();
    return true;
}

bool parseDate(const QString &text, QDate *date)
{
    static const QRegularExpression iso(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
    static const QRegularExpression usual(QStringLiteral("^(\\d{1,2})[/-](\\d{1,2})(?:[/-](\\d{2}|\\d{4}))?$"));
    QRegularExpressionMatch m = iso.match(text);
    if (m.hasMatch()) {
        *date = QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        return date->isValid();
    }
    m = usual.match(text);
    if (!m.hasMatch()) {
        return false;
    }
    int year = QDate::currentDate().year();
    if (!m.captured(3).isEmpty()) {
        year = m.captured(3).toInt();
        if (m.captured(3).size() == 2) {
            year += year < 30 ? 2000 : 1900;
        }
    }
    *date = QDate(year, m.captured(2).toInt(), m.captured(1).toInt());
    return date->isValid();
}
}

Value NumFmt::parseInput(const QString &typed, QString *code)
{
    const QString text = typed.trimmed();
    if (text.isEmpty()) {
        return typed.isEmpty() ? Value() : Value(typed);
    }
    if (typed.startsWith(QLatin1Char('\''))) {
        return Value(typed.mid(1));
    }
    const QString upper = text.toUpper();
    if (upper == QLatin1String("TRUE") || upper == QLatin1String("VERDADERO")) {
        return Value::boolean(true);
    }
    if (upper == QLatin1String("FALSE") || upper == QLatin1String("FALSO")) {
        return Value::boolean(false);
    }
    if (text.startsWith(QLatin1Char('#'))) {
        const Err error = Value::errorFromText(text);
        if (error != Err::None) {
            return Value(error);
        }
    }
    auto suggest = [code](const QString &format) {
        if (code) {
            *code = format;
        }
    };
    double number = parseNumber(text);
    if (!std::isnan(number)) {
        return Value(number);
    }
    if (text.endsWith(QLatin1Char('%'))) {
        const QString body = text.chopped(1).trimmed();
        number = parseNumber(body);
        if (!std::isnan(number)) {
            suggest(body.contains(QLatin1Char(',')) || (body.contains(QLatin1Char('.')) && number != std::floor(number)) ? QStringLiteral("0.00%") : QStringLiteral("0%"));
            return Value(number / 100.0);
        }
    }
    const QChar euro(0x20ac);
    if (text.endsWith(euro) || text.startsWith(euro) || text.startsWith(QLatin1Char('$')) || text.endsWith(QLatin1Char('$'))) {
        QString body = text;
        const bool dollar = body.contains(QLatin1Char('$'));
        body.remove(euro).remove(QLatin1Char('$'));
        number = parseNumber(body);
        if (!std::isnan(number)) {
            suggest(dollar ? QStringLiteral("[$$-409]#,##0.00") : QStringLiteral("#,##0.00\\ \"€\""));
            return Value(number);
        }
    }
    double fraction = 0;
    bool withSeconds = false;
    if (parseTime(text, &fraction, &withSeconds)) {
        suggest(withSeconds ? QStringLiteral("hh:mm:ss") : QStringLiteral("hh:mm"));
        return Value(fraction);
    }
    QDate day;
    if (parseDate(text, &day)) {
        suggest(QStringLiteral("dd/mm/yyyy"));
        return Value(serial(day));
    }
    const int space = text.indexOf(QLatin1Char(' '));
    const int tee = text.indexOf(QLatin1Char('T'));
    const int cut = space > 0 ? space : tee;
    if (cut > 0 && parseDate(text.left(cut), &day) && parseTime(text.mid(cut + 1), &fraction, &withSeconds)) {
        suggest(withSeconds ? QStringLiteral("dd/mm/yyyy hh:mm:ss") : QStringLiteral("dd/mm/yyyy hh:mm"));
        return Value(serial(day) + fraction);
    }
    return Value(typed);
}
