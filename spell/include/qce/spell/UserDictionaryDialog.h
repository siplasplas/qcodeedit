#pragma once

#include <memory>

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
    ~UserDictionaryDialog();
    explicit UserDictionaryDialog(SpellChecker& checker, QWidget* parent = nullptr);

private:
    void fill();

    // The state, in UserDictionaryDialog.cpp: fields added there keep the
    // binary interface (the size of this class does not change).
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace qce::spell
