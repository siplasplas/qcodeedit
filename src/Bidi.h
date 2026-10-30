#pragma once

#include <QString>
#include <QVector>

namespace qce::bidi {

/// The Unicode Bidirectional Algorithm (UAX #9) for one line: the embedding
/// level of every character, and the order in which the characters of a row
/// stand on the screen. The line is a paragraph whose direction is that of
/// its first strong character (Hebrew or Arabic: right to left; Latin or
/// none: left to right). Bidi classes come from QChar::direction().
///
/// Left out: explicit embeddings, overrides and isolates (LRE, RLO, LRI,
/// PDF, ... count as boundary neutrals and are not followed) and bracket
/// pairs (rule N0); brackets are resolved as other neutrals.

/// True when line[from, to) (to < 0: to the end) has a character written
/// right to left (Hebrew, Arabic, ..., or an RLE/RLO/RLI): only then can the
/// order on screen differ from the logical one. A line of Latin text costs
/// one comparison a character.
bool hasRightToLeft(const QString& line, int from = 0, int to = -1);

struct Levels {
    /// 0 for a left-to-right paragraph, 1 for a right-to-left one.
    int paragraph = 0;
    /// The level of each QChar (both halves of a surrogate pair have the
    /// same): even left to right, odd right to left. Rules W1–I2 and the
    /// part of L1 about segment separators (tabs) are applied; whitespace at
    /// the end of a row is left to visualOrder().
    QVector<quint8> levels;
};

/// The embedding levels of `line`.
Levels levels(const QString& line);

/// The code points of the row line[from, to) in the order they stand on
/// the screen, left to right, as indices of their first QChar. Whitespace
/// at the end of the row takes the paragraph's level (rule L1); then runs of
/// levels are reversed from the highest level down to the lowest odd one
/// (rule L2).
QVector<int> visualOrder(const QString& line, const Levels& levels, int from, int to);

} // namespace qce::bidi
