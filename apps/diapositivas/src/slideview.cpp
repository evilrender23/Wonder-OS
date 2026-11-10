#include "slideview.h"

#include <QPainter>

SlideView::SlideView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &SlideView::indexChanged, this, [this] { update(); });
    connect(this, &SlideView::optionsChanged, this, [this] { update(); });
}

void SlideView::setDeck(Deck *deck)
{
    if (deck == m_deck) {
        return;
    }
    if (m_deck) {
        disconnect(m_deck, nullptr, this, nullptr);
    }
    m_deck = deck;
    if (m_deck) {
        connect(m_deck, &Deck::changed, this, [this] { update(); });
        connect(m_deck, &QObject::destroyed, this, [this] { m_deck = nullptr; });
    }
    Q_EMIT deckChanged();
    update();
}

void SlideView::setIndex(int index)
{
    if (index != m_index) {
        m_index = index;
        Q_EMIT indexChanged();
    }
}

void SlideView::setEditing(bool editing)
{
    if (editing != m_options.editing) {
        m_options.editing = editing;
        Q_EMIT optionsChanged();
    }
}

void SlideView::setSkipText(const QString &id)
{
    if (id != m_options.skipText) {
        m_options.skipText = id;
        Q_EMIT optionsChanged();
    }
}

void SlideView::setStep(int step)
{
    if (step != m_options.step) {
        m_options.step = step;
        Q_EMIT optionsChanged();
    }
}

void SlideView::setOnly(const QString &id)
{
    if (id != m_options.only) {
        m_options.only = id;
        Q_EMIT optionsChanged();
    }
}

void SlideView::setTheme(const QString &id)
{
    if (id != m_options.theme) {
        m_options.theme = id;
        Q_EMIT optionsChanged();
    }
}

void SlideView::paint(QPainter *painter)
{
    if (!m_deck || m_index < 0 || m_index >= m_deck->count() || width() <= 0 || height() <= 0) {
        return;
    }
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    painter->scale(width() / m_deck->width(), height() / m_deck->height());
    Render::paint(*painter, m_deck->data(), m_index, m_options);
}
