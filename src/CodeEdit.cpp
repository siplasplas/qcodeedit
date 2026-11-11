#include "qce/CodeEdit.h"
#include "qce/CodeEditArea.h"
#include "qce/IMargin.h"
#include "qce/LeftRail.h"
#include "qce/RightRail.h"

#include <QHBoxLayout>
#include <QPalette>
#include <QScrollBar>

namespace qce {

struct CodeEdit::Private {
    CodeEditArea* m_area        = nullptr;
    LeftRail*     m_leftRail    = nullptr;
    RightRail*    m_rightRail   = nullptr;
    QScrollBar*   m_leftVScroll = nullptr; // non-null only in Left mode
    ScrollBarSide m_scrollBarSide = ScrollBarSide::Right;
};

CodeEdit::CodeEdit(QWidget* parent)
    :
    QWidget(parent),
      d(std::make_unique<Private>()) {

    d->m_area      = new CodeEditArea(this);
    d->m_leftRail  = new LeftRail(this);
    d->m_rightRail = new RightRail(this);

    d->m_leftRail->connectToArea(d->m_area);
    d->m_rightRail->connectToArea(d->m_area);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(d->m_leftRail);
    layout->addWidget(d->m_area, /*stretch=*/1);
    layout->addWidget(d->m_rightRail);
    setLayout(layout);

    // Built-in default theme: black text on white, independent of the system
    // palette, so syntax colours meant for a light background stay readable
    // under a dark desktop theme. Set on the container so that a palette the
    // application sets here still propagates to the area.
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::Text, Qt::black);
    setPalette(pal);
}

CodeEdit::~CodeEdit() = default;

void CodeEdit::setDocument(ITextDocument* doc) {
    d->m_area->setDocument(doc);
}

ITextDocument* CodeEdit::document() const {
    return d->m_area->document();
}

void CodeEdit::addLeftMargin(IMargin* margin) {
    d->m_leftRail->addMargin(margin);
}

void CodeEdit::removeLeftMargin(IMargin* margin) {
    d->m_leftRail->removeMargin(margin);
}

void CodeEdit::addRightMargin(IMargin* margin) {
    d->m_rightRail->addMargin(margin);
}

void CodeEdit::removeRightMargin(IMargin* margin) {
    d->m_rightRail->removeMargin(margin);
}

void CodeEdit::setGutterColors(const GutterColors& colors) {
    d->m_leftRail->setColors(colors);
    d->m_rightRail->setColors(colors);
}

GutterColors CodeEdit::gutterColors() const {
    return d->m_leftRail->colors();
}

void CodeEdit::setScrollBarSide(ScrollBarSide side) {
    if (d->m_scrollBarSide == side) {
        return;
    }
    d->m_scrollBarSide = side;
    applyScrollBarSide(side);
}

// ------------------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------------------

void CodeEdit::applyScrollBarSide(ScrollBarSide side) {
    QScrollBar* areaVBar = d->m_area->verticalScrollBar();
    auto* hbox = qobject_cast<QHBoxLayout*>(layout());

    if (side == ScrollBarSide::Left) {
        // Hide the area's built-in vertical scroll bar.
        d->m_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        // Create the standalone scroll bar and insert it at position 0.
        d->m_leftVScroll = new QScrollBar(Qt::Vertical, this);
        d->m_leftVScroll->setRange(areaVBar->minimum(), areaVBar->maximum());
        d->m_leftVScroll->setPageStep(areaVBar->pageStep());
        d->m_leftVScroll->setSingleStep(areaVBar->singleStep());
        d->m_leftVScroll->setValue(areaVBar->value());
        hbox->insertWidget(0, d->m_leftVScroll);

        // Two-way sync: standalone → area.
        connect(d->m_leftVScroll, &QScrollBar::valueChanged,
                areaVBar, &QScrollBar::setValue);
        // Two-way sync: area → standalone.
        connect(areaVBar, &QScrollBar::valueChanged,
                d->m_leftVScroll, &QScrollBar::setValue);
        // Keep range in sync when document changes.
        connect(areaVBar, &QScrollBar::rangeChanged,
                d->m_leftVScroll, &QScrollBar::setRange);

    } else {
        // Restore the area's built-in scroll bar.
        d->m_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        if (d->m_leftVScroll) {
            disconnect(d->m_leftVScroll, nullptr, areaVBar, nullptr);
            disconnect(areaVBar, nullptr, d->m_leftVScroll, nullptr);
            hbox->removeWidget(d->m_leftVScroll);
            delete d->m_leftVScroll;
            d->m_leftVScroll = nullptr;
        }
    }
}


CodeEditArea* CodeEdit::area() const { return d->m_area; }
CodeEdit::ScrollBarSide CodeEdit::scrollBarSide() const { return d->m_scrollBarSide; }

} // namespace qce
