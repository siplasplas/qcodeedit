#pragma once

#include <QDialog>

class QListWidget;
class QTreeWidget;

namespace qce::spell {

class SpellChecker;

/// The user's own words and corrections of the checker's first dictionary
/// (SpellChecker::userWords(), userCorrections()), shared by all programs:
/// to look them over and remove those no longer wanted. Changes are made at
/// once; recheck the editors afterwards.
class UserDictionaryDialog : public QDialog {
    Q_OBJECT
public:
    explicit UserDictionaryDialog(SpellChecker& checker, QWidget* parent = nullptr);

private:
    void fill();

    SpellChecker& m_checker;
    QListWidget* m_words = nullptr;
    QTreeWidget* m_corrections = nullptr;
};

} // namespace qce::spell
