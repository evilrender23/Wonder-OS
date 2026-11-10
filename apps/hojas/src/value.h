#pragma once

#include <QString>

/// The errors a cell can hold.
enum class Err : quint8 { None, Div0, Value, Ref, Name, NA, Num, Null, Cycle };

/// What a cell holds or a formula gives: nothing, a number (dates and times
/// are numbers too, days since 30 December 1899), text, true/false or an error.
struct Value {
    enum Type : quint8 { Empty, Number, Text, Bool, Error };

    Type type = Empty;
    Err err = Err::None;
    double num = 0;
    QString text;

    Value() = default;
    Value(double number)
        : type(Number)
        , num(number)
    {
    }
    Value(const QString &string)
        : type(Text)
        , text(string)
    {
    }
    Value(Err error)
        : type(Error)
        , err(error)
    {
    }
    static Value boolean(bool on)
    {
        Value v;
        v.type = Bool;
        v.num = on ? 1 : 0;
        return v;
    }

    bool isEmpty() const { return type == Empty; }
    bool isNumber() const { return type == Number; }
    bool isText() const { return type == Text; }
    bool isBool() const { return type == Bool; }
    bool isError() const { return type == Error; }
    bool truth() const { return num != 0; }

    bool operator==(const Value &other) const
    {
        if (type != other.type) {
            return false;
        }
        switch (type) {
        case Empty:
            return true;
        case Number:
        case Bool:
            return num == other.num;
        case Text:
            return text == other.text;
        case Error:
            return err == other.err;
        }
        return false;
    }
    bool operator!=(const Value &other) const { return !(*this == other); }

    /// «#DIV/0!» and the like.
    static QString errorText(Err error);
    /// The error a text spells, or None.
    static Err errorFromText(const QString &text);
};
