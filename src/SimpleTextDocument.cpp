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

struct SimpleTextDocument::Private {
    QStringList m_lines{QString()};
    mutable int m_maxLineLength = -1;
};

SimpleTextDocument::SimpleTextDocument(QObject* parent)
    :
    ITextDocument(parent),
      d(std::make_unique<Private>()) {
}

int SimpleTextDocument::lineCount() const {
    return d->m_lines.size();
}

QString SimpleTextDocument::lineAt(int index) const {
    if (index < 0 || index >= d->m_lines.size()) {
        return {};
    }
    return d->m_lines.at(index);
}

int SimpleTextDocument::maxLineLength() const {
    if (d->m_maxLineLength < 0) {
        int maxLen = 0;
        for (const QString& line : d->m_lines) {
            maxLen = std::max(maxLen, (int)line.size());
        }
        d->m_maxLineLength = maxLen;
    }
    return d->m_maxLineLength;
}

// --- Mutating interface --------------------------------------------------

TextCursor SimpleTextDocument::insertText(TextCursor pos, const QString& text) {
    if (d->m_lines.isEmpty()) {
        d->m_lines.append(QString());
    }
    pos = clampPos(pos);

    const QStringList parts = text.split(QLatin1Char('\n'));
    const QString prefix = d->m_lines.at(pos.line).left(pos.column);
    const QString suffix = d->m_lines.at(pos.line).mid(pos.column);

    if (parts.size() == 1) {
        d->m_lines[pos.line] = prefix + parts.first() + suffix;
        invalidateCache();
        emit linesChanged(pos.line, 1);
        return {pos.line, pos.column + int(parts.first().size())};
    }

    // Multi-line: modify current line, then insert new ones after it.
    d->m_lines[pos.line] = prefix + parts.first();
    // Room for the new lines at once, then filled: one line at a time moves
    // the rest of the list each time.
    d->m_lines.insert(pos.line + 1, parts.size() - 1, QString());
    for (int i = 1; i < parts.size(); ++i) {
        d->m_lines[pos.line + i] = (i == parts.size() - 1) ? parts.at(i) + suffix : parts.at(i);
    }

    invalidateCache();
    const int insertCount = parts.size() - 1;
    emit linesChanged(pos.line, 1);
    emit linesInserted(pos.line + 1, insertCount);
    return {pos.line + insertCount, int(parts.last().size())};
}

QString SimpleTextDocument::removeText(TextCursor start, TextCursor end) {
    if (d->m_lines.isEmpty() || start == end) {
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
        const QString removed = d->m_lines.at(start.line)
                                    .mid(start.column, end.column - start.column);
        d->m_lines[start.line].remove(start.column, end.column - start.column);
        invalidateCache();
        emit linesChanged(start.line, 1);
        return removed;
    }

    // Multi-line: collect removed text.
    QString removed = d->m_lines.at(start.line).mid(start.column);
    for (int i = start.line + 1; i < end.line; ++i) {
        removed += QLatin1Char('\n') + d->m_lines.at(i);
    }
    removed += QLatin1Char('\n') + d->m_lines.at(end.line).left(end.column);

    const QString joined = d->m_lines.at(start.line).left(start.column)
                           + d->m_lines.at(end.line).mid(end.column);

    const int removeCount = end.line - start.line;
    // At once: one line at a time moves the rest of the list each time.
    d->m_lines.remove(start.line + 1, removeCount);
    d->m_lines[start.line] = joined;

    invalidateCache();
    emit linesRemoved(start.line + 1, removeCount);
    emit linesChanged(start.line, 1);
    return removed;
}

// --- Bulk operations ----------------------------------------------------

void SimpleTextDocument::setLines(QStringList lines) {
    d->m_lines = std::move(lines);
    if (d->m_lines.isEmpty()) {
        d->m_lines.append(QString());
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
    const QVector<LineChange> changes = diff(d->m_lines, lines);
    if (changes.isEmpty()) return;
    emit textReplacementStarted();
    for (auto it = changes.crbegin(); it != changes.crend(); ++it) {
        const QStringList removed = d->m_lines.mid(it->oldStart, it->oldCount);
        const QStringList inserted = lines.mid(it->newStart, it->newCount);
        replaceLines(it->oldStart, it->oldCount, inserted);
        emit linesReplaced(it->oldStart, removed, inserted);
    }
    emit textReplacementFinished();
}

QString SimpleTextDocument::toPlainText() const {
    return d->m_lines.join(QLatin1Char('\n'));
}

// --- Private helpers ----------------------------------------------------

void SimpleTextDocument::invalidateCache() {
    d->m_maxLineLength = -1;
}

TextCursor SimpleTextDocument::clampPos(TextCursor pos) const {
    if (d->m_lines.isEmpty()) {
        return {0, 0};
    }
    pos.line   = qBound(0, pos.line,   d->m_lines.size() - 1);
    pos.column = qBound(0, pos.column, d->m_lines.at(pos.line).size());
    return pos;
}

SimpleTextDocument::~SimpleTextDocument() = default;

} // namespace qce
