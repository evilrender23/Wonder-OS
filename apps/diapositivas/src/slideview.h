#pragma once

#include "deck.h"
#include "render.h"

#include <QQuickPaintedItem>

/// One slide of the deck, painted to the size of the item: the canvas, a
/// thumbnail, or what the audience sees.
class SlideView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Deck *deck READ deck WRITE setDeck NOTIFY deckChanged)
    Q_PROPERTY(int index READ index WRITE setIndex NOTIFY indexChanged)
    /// Shows the prompts of the empty placeholders.
    Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY optionsChanged)
    /// The item whose text an editor is showing in its place.
    Q_PROPERTY(QString skipText READ skipText WRITE setSkipText NOTIFY optionsChanged)
    /// How many builds have come in; -1 for all.
    Q_PROPERTY(int step READ step WRITE setStep NOTIFY optionsChanged)
    /// Only this item, on nothing.
    Q_PROPERTY(QString only READ only WRITE setOnly NOTIFY optionsChanged)
    /// A theme to show the slide in instead of the deck's.
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY optionsChanged)

public:
    explicit SlideView(QQuickItem *parent = nullptr);

    Deck *deck() const { return m_deck; }
    void setDeck(Deck *deck);
    int index() const { return m_index; }
    void setIndex(int index);
    bool editing() const { return m_options.editing; }
    void setEditing(bool editing);
    QString skipText() const { return m_options.skipText; }
    void setSkipText(const QString &id);
    int step() const { return m_options.step; }
    void setStep(int step);
    QString only() const { return m_options.only; }
    void setOnly(const QString &id);
    QString theme() const { return m_options.theme; }
    void setTheme(const QString &id);

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void deckChanged();
    void indexChanged();
    void optionsChanged();

private:
    Deck *m_deck = nullptr;
    Render::Options m_options;
    int m_index = 0;
};
