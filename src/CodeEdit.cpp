#include "qce/CodeEdit.h"
#include "qce/CodeEditArea.h"
#include "qce/IMargin.h"
#include "qce/LeftRail.h"
#include "qce/RightRail.h"

#include <QHBoxLayout>
#include <QScrollBar>

namespace qce {

CodeEdit::CodeEdit(QWidget* parent)
    : QWidget(parent) {
    m_area      = new CodeEditArea(this);
    m_leftRail  = new LeftRail(this);
    m_rightRail = new RightRail(this);

    m_leftRail->connectToArea(m_area);
    m_rightRail->connectToArea(m_area);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_leftRail);
    layout->addWidget(m_area, /*stretch=*/1);
    layout->addWidget(m_rightRail);
    setLayout(layout);
}

CodeEdit::~CodeEdit() = default;

void CodeEdit::setDocument(ITextDocument* doc) {
    m_area->setDocument(doc);
}

ITextDocument* CodeEdit::document() const {
    return m_area->document();
}

void CodeEdit::addLeftMargin(IMargin* margin) {
    m_leftRail->addMargin(margin);
}

void CodeEdit::addRightMargin(IMargin* margin) {
    m_rightRail->addMargin(margin);
}

void CodeEdit::setScrollBarSide(ScrollBarSide side) {
    if (m_scrollBarSide == side) {
        return;
    }
    m_scrollBarSide = side;
    applyScrollBarSide(side);
}

// ------------------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------------------

void CodeEdit::applyScrollBarSide(ScrollBarSide side) {
    QScrollBar* areaVBar = m_area->verticalScrollBar();
    auto* hbox = qobject_cast<QHBoxLayout*>(layout());

    if (side == ScrollBarSide::Left) {
        // Hide the area's built-in vertical scroll bar.
        m_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        // Create the standalone scroll bar and insert it at position 0.
        m_leftVScroll = new QScrollBar(Qt::Vertical, this);
        m_leftVScroll->setRange(areaVBar->minimum(), areaVBar->maximum());
        m_leftVScroll->setPageStep(areaVBar->pageStep());
        m_leftVScroll->setSingleStep(areaVBar->singleStep());
        m_leftVScroll->setValue(areaVBar->value());
        hbox->insertWidget(0, m_leftVScroll);

        // Two-way sync: standalone → area.
        connect(m_leftVScroll, &QScrollBar::valueChanged,
                areaVBar, &QScrollBar::setValue);
        // Two-way sync: area → standalone.
        connect(areaVBar, &QScrollBar::valueChanged,
                m_leftVScroll, &QScrollBar::setValue);
        // Keep range in sync when document changes.
        connect(areaVBar, &QScrollBar::rangeChanged,
                m_leftVScroll, &QScrollBar::setRange);

    } else {
        // Restore the area's built-in scroll bar.
        m_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        if (m_leftVScroll) {
            disconnect(m_leftVScroll, nullptr, areaVBar, nullptr);
            disconnect(areaVBar, nullptr, m_leftVScroll, nullptr);
            hbox->removeWidget(m_leftVScroll);
            delete m_leftVScroll;
            m_leftVScroll = nullptr;
        }
    }
}

} // namespace qce
