#include "qce/SimpleTextDocument.h"

namespace qce {

SimpleTextDocument::SimpleTextDocument(QObject* parent)
    : ITextDocument(parent) {}

int SimpleTextDocument::lineCount() const {
    return m_lines.size();
}

QString SimpleTextDocument::lineAt(int index) const {
    if (index < 0 || index >= m_lines.size()) {
        return QString();
    }
    return m_lines.at(index);
}

int SimpleTextDocument::maxLineLength() const {
    // Lazy cache: compute once, invalidate on mutation.
    if (m_maxLineLength < 0) {
        int maxLen = 0;
        for (const QString& line : m_lines) {
            if (line.size() > maxLen) {
                maxLen = line.size();
            }
        }
        m_maxLineLength = maxLen;
    }
    return m_maxLineLength;
}

void SimpleTextDocument::setLines(QStringList lines) {
    m_lines = std::move(lines);
    invalidateCache();
    emit documentReset();
}

void SimpleTextDocument::setText(const QString& text) {
    // Normalize CRLF to LF, then split.
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    // QString::split with KeepEmptyParts preserves empty lines between
    // separators. A trailing '\n' would produce an empty trailing element;
    // we strip it so that "abc\n" is one line, not two.
    QStringList lines = normalized.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }

    setLines(std::move(lines));
}

QString SimpleTextDocument::toPlainText() const {
    return m_lines.join(QLatin1Char('\n'));
}

void SimpleTextDocument::invalidateCache() {
    m_maxLineLength = -1;
}

} // namespace qce
