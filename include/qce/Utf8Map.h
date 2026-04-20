#pragma once

#include <QVector>

#include <cstddef>

namespace qce {

/// Maps positions in a UTF-8 byte buffer to positions in the same text
/// decoded as a QString (UTF-16), and back.
///
/// IHighlighter::highlightLine() produces spans whose `start`/`length` are
/// QChar indices; byte-range consumers (e.g. viewers that tokenise mmap'd
/// UTF-8 directly) need those spans translated to byte offsets. This class
/// is the single place that translation lives.
///
/// Built once per line/range via buildFromUtf8(), then queried O(1) with
/// byteForQChar() / qcharForByte().
///
/// UTF-8 → UTF-16 accounting:
/// - 1/2/3-byte sequences → 1 QChar
/// - 4-byte sequences     → 2 QChars (surrogate pair)
/// - Malformed bytes      → 1 QChar each (Qt::fromUtf8 replaces with U+FFFD)
///
/// All positions use [0, count()] bounds, i.e. the past-the-end index is
/// legal and returns the past-the-end counterpart.
///
/// Surrogate-pair semantics: both halves of a surrogate pair share the
/// same byte offset (the start of the 4-byte code point). Consequently a
/// degenerate span that ends between the halves maps to an empty byte
/// range — half a code point has no UTF-8 representation. Spans produced
/// by highlighters normally respect code-point boundaries, so this only
/// matters for defensive callers.
class Utf8Map {
public:
    void buildFromUtf8(const char* data, qsizetype len);

    int       qcharCount() const { return m_qcharCount; }
    qsizetype byteCount()  const { return m_byteCount;  }

    /// byteForQChar(qcharCount()) == byteCount(). qcharIndex is clamped.
    qsizetype byteForQChar(int qcharIndex) const;

    /// qcharForByte(byteCount()) == qcharCount(). byteOffset is clamped.
    /// If byteOffset falls inside a multi-byte UTF-8 sequence, returns the
    /// QChar index of the code point that sequence encodes.
    int qcharForByte(qsizetype byteOffset) const;

private:
    // m_byteOf[i] = byte offset where the i-th QChar begins.
    // Size = m_qcharCount + 1; last entry == m_byteCount.
    QVector<qsizetype> m_byteOf;
    // m_qcharOf[b] = QChar index the byte at offset b belongs to.
    // Size = m_byteCount + 1; last entry == m_qcharCount.
    QVector<int>       m_qcharOf;
    int                m_qcharCount = 0;
    qsizetype          m_byteCount  = 0;
};

} // namespace qce
