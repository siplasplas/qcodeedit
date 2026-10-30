#include "Bidi.h"

#include <qce/CodePoints.h>

#include <algorithm>

namespace qce::bidi {

namespace {

char32_t codePointAt(const QString& text, int index) {
    return codepoints::isPairAt(text, index) ? QChar::surrogateToUcs4(text.at(index), text.at(index + 1))
                                             : char32_t(text.at(index).unicode());
}

// Explicit formatting characters: not followed, treated as boundary
// neutrals (as rule X9 removes them).
bool isExplicit(QChar::Direction d) {
    switch (d) {
    case QChar::DirLRE: case QChar::DirLRO: case QChar::DirRLE: case QChar::DirRLO:
    case QChar::DirPDF: case QChar::DirLRI: case QChar::DirRLI: case QChar::DirFSI:
    case QChar::DirPDI:
        return true;
    default:
        return false;
    }
}

bool isNeutral(QChar::Direction d) {
    return d == QChar::DirB || d == QChar::DirS || d == QChar::DirWS || d == QChar::DirON;
}

// For rules N1 and the like: numbers count as right to left.
QChar::Direction strongForNeutrals(QChar::Direction d) {
    return (d == QChar::DirEN || d == QChar::DirAN) ? QChar::DirR : d;
}

} // namespace

bool hasRightToLeft(const QString& line, int from, int to) {
    if (to < 0 || to > line.size()) to = int(line.size());
    for (int i = qMax(0, from); i < to; ++i) {
        const char16_t u = line.at(i).unicode();
        if (u < 0x0590) continue;                // Latin, Greek, Cyrillic, ...
        const QChar::Direction d = QChar::direction(codePointAt(line, i));
        if (d == QChar::DirR || d == QChar::DirAL || d == QChar::DirRLE || d == QChar::DirRLO
            || d == QChar::DirRLI)
            return true;
    }
    return false;
}

Levels levels(const QString& line) {
    Levels result;
    result.levels.resize(line.size());
    // The code points, with their bidi classes.
    QVector<int> starts;
    QVector<QChar::Direction> types;
    starts.reserve(line.size());
    types.reserve(line.size());
    for (int i = 0; i < line.size(); i = codepoints::nextIndex(line, i)) {
        starts << i;
        types << QChar::direction(codePointAt(line, i));
    }
    const int n = int(starts.size());

    // P2, P3: the first strong character gives the paragraph's direction.
    for (const QChar::Direction d : types) {
        if (d == QChar::DirL) break;
        if (d == QChar::DirR || d == QChar::DirAL) {
            result.paragraph = 1;
            break;
        }
    }
    const int paragraph = result.paragraph;
    const QChar::Direction embedding = paragraph ? QChar::DirR : QChar::DirL;  // also sos and eos

    // X9: boundary neutrals and explicit codes are left out of the rules
    // below; `kept` are the others, `t` their classes being resolved.
    QVector<int> kept;
    kept.reserve(n);
    for (int k = 0; k < n; ++k)
        if (types[k] != QChar::DirBN && !isExplicit(types[k])) kept << k;
    const int m = int(kept.size());
    QVector<QChar::Direction> t(m);
    for (int j = 0; j < m; ++j) t[j] = types[kept[j]];

    // W1: a non-spacing mark takes the class of the character before it.
    for (int j = 0; j < m; ++j)
        if (t[j] == QChar::DirNSM) t[j] = j > 0 ? t[j - 1] : embedding;
    // W2: a European number after Arabic letters is an Arabic number.
    QChar::Direction lastStrong = embedding;
    for (int j = 0; j < m; ++j) {
        if (t[j] == QChar::DirL || t[j] == QChar::DirR || t[j] == QChar::DirAL) lastStrong = t[j];
        else if (t[j] == QChar::DirEN && lastStrong == QChar::DirAL) t[j] = QChar::DirAN;
    }
    // W3: Arabic letters are right to left.
    for (int j = 0; j < m; ++j)
        if (t[j] == QChar::DirAL) t[j] = QChar::DirR;
    // W4: one separator between two numbers of a kind joins them ("1,5").
    for (int j = 1; j + 1 < m; ++j) {
        if (t[j] == QChar::DirES && t[j - 1] == QChar::DirEN && t[j + 1] == QChar::DirEN) t[j] = QChar::DirEN;
        else if (t[j] == QChar::DirCS && t[j - 1] == t[j + 1]
                 && (t[j - 1] == QChar::DirEN || t[j - 1] == QChar::DirAN))
            t[j] = t[j - 1];
    }
    // W5: terminators next to a European number belong to it ("$5", "5%").
    for (int j = 0; j < m;) {
        if (t[j] != QChar::DirET) {
            ++j;
            continue;
        }
        int end = j;
        while (end < m && t[end] == QChar::DirET) ++end;
        if ((j > 0 && t[j - 1] == QChar::DirEN) || (end < m && t[end] == QChar::DirEN))
            std::fill(t.begin() + j, t.begin() + end, QChar::DirEN);
        j = end;
    }
    // W6: other separators and terminators are neutral.
    for (int j = 0; j < m; ++j)
        if (t[j] == QChar::DirES || t[j] == QChar::DirET || t[j] == QChar::DirCS) t[j] = QChar::DirON;
    // W7: a European number after Latin letters is left to right.
    lastStrong = embedding;
    for (int j = 0; j < m; ++j) {
        if (t[j] == QChar::DirL || t[j] == QChar::DirR) lastStrong = t[j];
        else if (t[j] == QChar::DirEN && lastStrong == QChar::DirL) t[j] = QChar::DirL;
    }
    // N1, N2: neutrals between characters of one direction take it, others
    // the paragraph's.
    for (int j = 0; j < m;) {
        if (!isNeutral(t[j])) {
            ++j;
            continue;
        }
        int end = j;
        while (end < m && isNeutral(t[end])) ++end;
        const QChar::Direction before = j > 0 ? strongForNeutrals(t[j - 1]) : embedding;
        const QChar::Direction after = end < m ? strongForNeutrals(t[end]) : embedding;
        std::fill(t.begin() + j, t.begin() + end, before == after ? before : embedding);
        j = end;
    }

    // I1, I2: the levels.
    QVector<quint8> cpLevels(n, quint8(paragraph));
    for (int j = 0; j < m; ++j) {
        int level = paragraph;
        if (paragraph == 0) {
            if (t[j] == QChar::DirR) level = 1;
            else if (t[j] == QChar::DirAN || t[j] == QChar::DirEN) level = 2;
        } else if (t[j] == QChar::DirL || t[j] == QChar::DirEN || t[j] == QChar::DirAN) {
            level = 2;
        }
        cpLevels[kept[j]] = quint8(level);
    }
    // The characters left out take the level of the one before them.
    for (int k = 0; k < n; ++k)
        if (types[k] == QChar::DirBN || isExplicit(types[k])) cpLevels[k] = k > 0 ? cpLevels[k - 1] : quint8(paragraph);
    // L1: segment separators (tabs) and the whitespace before them are at
    // the paragraph's level.
    for (int k = 0; k < n; ++k) {
        if (types[k] != QChar::DirS && types[k] != QChar::DirB) continue;
        cpLevels[k] = quint8(paragraph);
        for (int b = k - 1; b >= 0 && (types[b] == QChar::DirWS || types[b] == QChar::DirBN || isExplicit(types[b]));
             --b)
            cpLevels[b] = quint8(paragraph);
    }

    for (int k = 0; k < n; ++k) {
        result.levels[starts[k]] = cpLevels[k];
        if (codepoints::isPairAt(line, starts[k])) result.levels[starts[k] + 1] = cpLevels[k];
    }
    return result;
}

QVector<int> visualOrder(const QString& line, const Levels& levels, int from, int to) {
    from = qBound(0, codepoints::toBoundary(line, from), int(line.size()));
    to = qBound(from, to, int(line.size()));
    QVector<int> order;
    QVector<quint8> rowLevels;
    for (int i = from; i < to; i = codepoints::nextIndex(line, i)) {
        order << i;
        rowLevels << (i < levels.levels.size() ? levels.levels[i] : quint8(levels.paragraph));
    }
    // L1: whitespace at the end of the row is at the paragraph's level.
    for (int k = int(order.size()) - 1; k >= 0; --k) {
        const QChar::Direction d = QChar::direction(codePointAt(line, order[k]));
        if (d != QChar::DirWS && d != QChar::DirBN && !isExplicit(d)) break;
        rowLevels[k] = quint8(levels.paragraph);
    }
    // L2: reverse every run at or above each level, from the highest down
    // to the lowest odd one.
    int highest = 0;
    int lowestOdd = 255;
    for (const quint8 level : rowLevels) {
        highest = qMax(highest, int(level));
        if (level % 2) lowestOdd = qMin(lowestOdd, int(level));
    }
    for (int level = highest; level >= lowestOdd && level > 0; --level) {
        for (int k = 0; k < order.size();) {
            if (rowLevels[k] < level) {
                ++k;
                continue;
            }
            int end = k;
            while (end < order.size() && rowLevels[end] >= level) ++end;
            std::reverse(order.begin() + k, order.begin() + end);
            std::reverse(rowLevels.begin() + k, rowLevels.begin() + end);
            k = end;
        }
    }
    return order;
}

} // namespace qce::bidi
