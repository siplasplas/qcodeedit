#include "qce/CodeEdit.h"
#include "qce/CodeEditArea.h"
#include "qce/IMargin.h"
#include "qce/LeftRail.h"
#include "qce/RightRail.h"

#include <QHBoxLayout>

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
    // Stub: stored but not yet acted upon (section 6.5).
    m_scrollBarSide = side;
}

} // namespace qce
