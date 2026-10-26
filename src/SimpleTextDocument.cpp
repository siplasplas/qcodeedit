#include "qce/SimpleTextDocument.h"
#include "qce/LineDiff.h"

#include <algorithm>

namespace qce {

namespace {
// A plain function pointer: set from static initializers of other
// libraries, so it must be constant-initialized.
LineDiffFn g_lineDiff = nullptr;
}

void setLineDiff(LineDiffFn fn) { g_lineDiff = fn; }
LineDiffFn lineDiff() { return g_lineDiff; }

SimpleTextDocument::SimpleTextDocument(QObject* parent)
    : ITextDocument(parent) {}

int SimpleTextDocument::lineCount() const {
    return m_lines.size();
}

QString SimpleTextDocument::lineAt(int index) const {
    if (index < 0 || index >= m_lines.size()) {
        return {};
    }
    return m_lines.at(index);
}

int SimpleTextDocument::maxLineLength() const {
    if (m_maxLineLength < 0) {
        int maxLen = 0;
        for (const QString& line : m_lines) {
            maxLen = std::max(maxLen, (int)line.size());
        }
        m_maxLineLength = maxLen;
    }
    return m_maxLineLength;
}

// --- Mutating interface --------------------------------------------------

TextCursor SimpleTextDocument::insertText(TextCursor pos, const QString& text) {
    if (m_lines.isEmpty()) {
        m_lines.append(QString());
    }
    pos = clampPos(pos);

    const QStringList parts = text.split(QLatin1Char('\n'));
    const QString prefix = m_lines.at(pos.line).left(pos.column);
    const QString suffix = m_lines.at(pos.line).mid(pos.column);

    if (parts.size() == 1) {
        m_lines[pos.line] = prefix + parts.first() + suffix;
        invalidateCache();
        emit linesChanged(pos.line, 1);
        return {pos.line, pos.column + int(parts.first().size())};
    }

    // Multi-line: modify current line, then insert new ones after it.
    m_lines[pos.line] = prefix + parts.first();
    // Room for the new lines at once, then filled: one line at a time moves
    // the rest of the list each time.
    m_lines.insert(pos.line + 1, parts.size() - 1, QString());
    for (int i = 1; i < parts.size(); ++i) {
        m_lines[pos.line + i] = (i == parts.size() - 1) ? parts.at(i) + suffix : parts.at(i);
    }

    invalidateCache();
    const int insertCount = parts.size() - 1;
    emit linesChanged(pos.line, 1);
    emit linesInserted(pos.line + 1, insertCount);
    return {pos.line + insertCount, int(parts.last().size())};
}

QString SimpleTextDocument::removeText(TextCursor start, TextCursor end) {
    if (m_lines.isEmpty() || start == end) {
        return {};
    }
    start = clampPos(start);
    end   = clampPos(end);

    if (end < start) {
        qSwap(start, end);
    }
    if (start == end) {
        return {};
    }

    if (start.line == end.line) {
        const QString removed = m_lines.at(start.line)
                                    .mid(start.column, end.column - start.column);
        m_lines[start.line].remove(start.column, end.column - start.column);
        invalidateCache();
        emit linesChanged(start.line, 1);
        return removed;
    }

    // Multi-line: collect removed text.
    QString removed = m_lines.at(start.line).mid(start.column);
    for (int i = start.line + 1; i < end.line; ++i) {
        removed += QLatin1Char('\n') + m_lines.at(i);
    }
    removed += QLatin1Char('\n') + m_lines.at(end.line).left(end.column);

    const QString joined = m_lines.at(start.line).left(start.column)
                           + m_lines.at(end.line).mid(end.column);

    const int removeCount = end.line - start.line;
    // At once: one line at a time moves the rest of the list each time.
    m_lines.remove(start.line + 1, removeCount);
    m_lines[start.line] = joined;

    invalidateCache();
    emit linesRemoved(start.line + 1, removeCount);
    emit linesChanged(start.line, 1);
    return removed;
}

// --- Bulk operations ----------------------------------------------------

void SimpleTextDocument::setLines(QStringList lines) {
    m_lines = std::move(lines);
    if (m_lines.isEmpty()) {
        m_lines.append(QString());
    }
    invalidateCache();
    emit documentReset();
}

void SimpleTextDocument::setText(const QString& text) {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    QStringList lines = normalized.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    if (lines.isEmpty()) {
        lines.append(QString());
    }
    const LineDiffFn diff = lineDiff();
    if (!diff) {
        setLines(std::move(lines));
        return;
    }
    // Only the lines that differ change, from the last change up, so that
    // the line numbers of the changes still to make stay valid.
    const QVector<LineChange> changes = diff(m_lines, lines);
    if (changes.isEmpty()) return;
    emit textReplacementStarted();
    for (auto it = changes.crbegin(); it != changes.crend(); ++it) {
        const QStringList removed = m_lines.mid(it->oldStart, it->oldCount);
        const QStringList inserted = lines.mid(it->newStart, it->newCount);
        replaceLines(it->oldStart, it->oldCount, inserted);
        emit linesReplaced(it->oldStart, removed, inserted);
    }
    emit textReplacementFinished();
}

QString SimpleTextDocument::toPlainText() const {
    return m_lines.join(QLatin1Char('\n'));
}

// --- Private helpers ----------------------------------------------------

void SimpleTextDocument::invalidateCache() {
    m_maxLineLength = -1;
}

TextCursor SimpleTextDocument::clampPos(TextCursor pos) const {
    if (m_lines.isEmpty()) {
        return {0, 0};
    }
    pos.line   = qBound(0, pos.line,   m_lines.size() - 1);
    pos.column = qBound(0, pos.column, m_lines.at(pos.line).size());
    return pos;
}

} // namespace qce
