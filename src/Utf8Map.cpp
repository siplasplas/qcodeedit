#include "qce/Utf8Map.h"

#include <cstdint>

namespace qce {

void Utf8Map::buildFromUtf8(const char* data, qsizetype len) {
    m_byteOf.clear();
    m_qcharOf.clear();
    m_byteCount = len;

    // Worst case: len QChars (ASCII) + 1 past-the-end.
    m_byteOf.reserve(len + 1);
    m_qcharOf.resize(len + 1);

    int       qi = 0;
    qsizetype bi = 0;

    while (bi < len) {
        const auto lead = static_cast<unsigned char>(data[bi]);

        int seqLen;
        if (lead < 0x80)              seqLen = 1;
        else if ((lead >> 5) == 0x06) seqLen = 2;  // 110xxxxx
        else if ((lead >> 4) == 0x0E) seqLen = 3;  // 1110xxxx
        else if ((lead >> 3) == 0x1E) seqLen = 4;  // 11110xxx
        else                          seqLen = 1;  // malformed — 1 byte, 1 QChar

        if (bi + seqLen > len) seqLen = 1; // truncated trailing sequence

        const bool surrogatePair = (seqLen == 4);

        // First QChar of this code point starts at bi.
        m_byteOf.append(bi);
        if (surrogatePair) {
            // High surrogate: all 4 bytes map to qi; low surrogate also sits at bi.
            m_byteOf.append(bi);
        }

        // Mark every byte of this sequence as belonging to the first QChar
        // (or the high surrogate, which has the lower QChar index).
        for (int k = 0; k < seqLen; ++k) {
            m_qcharOf[bi + k] = qi;
        }

        qi += surrogatePair ? 2 : 1;
        bi += seqLen;
    }

    // Past-the-end sentinel — lets byteForQChar(qcharCount()) and
    // qcharForByte(byteCount()) return end positions without branching.
    m_byteOf.append(len);
    m_qcharOf[len] = qi;
    m_qcharCount = qi;
}

qsizetype Utf8Map::byteForQChar(int qcharIndex) const {
    if (qcharIndex < 0) qcharIndex = 0;
    if (qcharIndex > m_qcharCount) qcharIndex = m_qcharCount;
    return m_byteOf[qcharIndex];
}

int Utf8Map::qcharForByte(qsizetype byteOffset) const {
    if (byteOffset < 0) byteOffset = 0;
    if (byteOffset > m_byteCount) byteOffset = m_byteCount;
    return m_qcharOf[byteOffset];
}

} // namespace qce
