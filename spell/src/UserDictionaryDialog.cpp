#include <qce/spell/UserDictionaryDialog.h>

#include <qce/spell/SpellChecker.h>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace qce::spell {

UserDictionaryDialog::UserDictionaryDialog(SpellChecker& checker, QWidget* parent)
    : QDialog(parent), m_checker(checker) {
    setWindowTitle(tr("User Dictionary"));
    resize(520, 440);
    auto* layout = new QVBoxLayout(this);
    const QStringList dictionaries = checker.dictionaries();
    auto* intro = new QLabel(tr("Your own words and corrections (%1), shared by all programs. A word here is "
                                "right; a correction here is the only one offered for its word, and sure.")
                                 .arg(dictionaries.isEmpty() ? tr("no dictionary") : dictionaries.front()),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* tabs = new QTabWidget(this);
    auto* wordsPage = new QWidget(tabs);
    auto* wordsLayout = new QVBoxLayout(wordsPage);
    m_words = new QListWidget(wordsPage);
    m_words->setSelectionMode(QAbstractItemView::ExtendedSelection);
    wordsLayout->addWidget(m_words);
    auto* removeWords = new QPushButton(tr("&Remove"), wordsPage);
    wordsLayout->addWidget(removeWords, 0, Qt::AlignLeft);
    tabs->addTab(wordsPage, tr("&Words"));

    auto* correctionsPage = new QWidget(tabs);
    auto* correctionsLayout = new QVBoxLayout(correctionsPage);
    m_corrections = new QTreeWidget(correctionsPage);
    m_corrections->setRootIsDecorated(false);
    m_corrections->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_corrections->setHeaderLabels({tr("Typed"), tr("Corrected to")});
    correctionsLayout->addWidget(m_corrections);
    auto* removeCorrections = new QPushButton(tr("R&emove"), correctionsPage);
    correctionsLayout->addWidget(removeCorrections, 0, Qt::AlignLeft);
    tabs->addTab(correctionsPage, tr("&Corrections"));
    layout->addWidget(tabs, 1);

    connect(removeWords, &QPushButton::clicked, this, [this]() {
        for (QListWidgetItem* item : m_words->selectedItems()) m_checker.removeFromDictionary(item->text());
        fill();
    });
    connect(removeCorrections, &QPushButton::clicked, this, [this]() {
        for (QTreeWidgetItem* item : m_corrections->selectedItems()) m_checker.removeCorrection(item->text(0));
        fill();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
    fill();
}

void UserDictionaryDialog::fill() {
    m_words->clear();
    m_words->addItems(m_checker.userWords());
    m_corrections->clear();
    for (const auto& [typed, correction] : m_checker.userCorrections())
        new QTreeWidgetItem(m_corrections, {typed, correction});
    m_corrections->resizeColumnToContents(0);
}

} // namespace qce::spell
