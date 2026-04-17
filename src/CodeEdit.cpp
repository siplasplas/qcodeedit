#include "qce/CodeEdit.h"
#include "qce/CodeEditArea.h"

#include <QHBoxLayout>

namespace qce {

CodeEdit::CodeEdit(QWidget* parent)
    : QWidget(parent) {
    m_area = new CodeEditArea(this);

    // v1: plain horizontal layout with only the area. Left/right rails
    // (gutters, margins) will be inserted in a later milestone.
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_area);
    setLayout(layout);
}

CodeEdit::~CodeEdit() = default;

void CodeEdit::setDocument(ITextDocument* doc) {
    m_area->setDocument(doc);
}

ITextDocument* CodeEdit::document() const {
    return m_area->document();
}

void CodeEdit::setScrollBarSide(ScrollBarSide side) {
    // v1 stub: store the preference but do not actually rearrange anything.
    // The real implementation requires hiding QAbstractScrollArea's built-in
    // scroll bar and placing our own QScrollBar in the left rail.
    m_scrollBarSide = side;
}

} // namespace qce
