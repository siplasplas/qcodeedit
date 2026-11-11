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

struct UserDictionaryDialog::Private {
    SpellChecker& m_checker;
    QListWidget* m_words = nullptr;
    QTreeWidget* m_corrections = nullptr;
};

UserDictionaryDialog::UserDictionaryDialog(SpellChecker& checker, QWidget* parent)
    :
    QDialog(parent),
      d(new Private{checker}) {
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
    d->m_words = new QListWidget(wordsPage);
    d->m_words->setSelectionMode(QAbstractItemView::ExtendedSelection);
    wordsLayout->addWidget(d->m_words);
    auto* removeWords = new QPushButton(tr("&Remove"), wordsPage);
    wordsLayout->addWidget(removeWords, 0, Qt::AlignLeft);
    tabs->addTab(wordsPage, tr("&Words"));

    auto* correctionsPage = new QWidget(tabs);
    auto* correctionsLayout = new QVBoxLayout(correctionsPage);
    d->m_corrections = new QTreeWidget(correctionsPage);
    d->m_corrections->setRootIsDecorated(false);
    d->m_corrections->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->m_corrections->setHeaderLabels({tr("Typed"), tr("Corrected to")});
    correctionsLayout->addWidget(d->m_corrections);
    auto* removeCorrections = new QPushButton(tr("R&emove"), correctionsPage);
    correctionsLayout->addWidget(removeCorrections, 0, Qt::AlignLeft);
    tabs->addTab(correctionsPage, tr("&Corrections"));
    layout->addWidget(tabs, 1);

    connect(removeWords, &QPushButton::clicked, this, [this]() {
        for (QListWidgetItem* item : d->m_words->selectedItems()) d->m_checker.removeFromDictionary(item->text());
        fill();
    });
    connect(removeCorrections, &QPushButton::clicked, this, [this]() {
        for (QTreeWidgetItem* item : d->m_corrections->selectedItems()) d->m_checker.removeCorrection(item->text(0));
        fill();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
    fill();
}

void UserDictionaryDialog::fill() {
    d->m_words->clear();
    d->m_words->addItems(d->m_checker.userWords());
    d->m_corrections->clear();
    for (const auto& [typed, correction] : d->m_checker.userCorrections())
        new QTreeWidgetItem(d->m_corrections, {typed, correction});
    d->m_corrections->resizeColumnToContents(0);
}


UserDictionaryDialog::~UserDictionaryDialog() = default;

} // namespace qce::spell
