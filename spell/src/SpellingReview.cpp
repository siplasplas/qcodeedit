#include <qce/spell/SpellingReview.h>

#include <qce/CodeEditArea.h>
#include <qce/ITextDocument.h>

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QMenu>
#include <QShortcut>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace qce::spell {

namespace {

enum Column { WordColumn, CorrectionColumn, ScoreColumn, LineColumn, ContextColumn };

// The correction a row's box had before the user picked one.
const char* const kChosenProperty = "qceChosen";

// Sort keys of a row, in Qt::UserRole + 1 of these columns.
constexpr int kSortRole = Qt::UserRole + 1;

// A row sorted by the sort key of the column (the text's order, the word
// alphabetically, or the score), keeping its correction box.
class ReviewRow : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override {
        const int column = treeWidget() ? treeWidget()->sortColumn() : LineColumn;
        const QVariant a = data(column, kSortRole), b = other.data(column, kSortRole);
        if (column == WordColumn) {
            const int order = QString::localeAwareCompare(a.toString(), b.toString());
            if (order != 0) return order < 0;
        } else if (a.toDouble() != b.toDouble()) {
            return a.toDouble() < b.toDouble();
        }
        return data(LineColumn, kSortRole).toDouble() < other.data(LineColumn, kSortRole).toDouble();
    }
};

const QStringList& checkedAtFirstNames() {
    static const QStringList names = {QStringLiteral("sure"), QStringLiteral("single"), QStringLiteral("every")};
    return names;
}

// The line around the word, shortened at both ends.
QString context(const QString& line, int start, int end) {
    constexpr int kAround = 30;
    const int from = qMax(0, start - kAround);
    const int to = qMin(int(line.size()), end + kAround);
    QString text = line.mid(from, to - from).simplified();
    if (from > 0) text.prepend(QStringLiteral("…"));
    if (to < line.size()) text.append(QStringLiteral("…"));
    return text;
}

// "mam yz → mamy z": a moved space with the words it changes.
QString spaceCorrectionLabel(const QString& typed, const QString& replacement) {
    return QStringLiteral("%1 → %2").arg(typed, replacement);
}

} // namespace

QString checkedAtFirstName(CheckedAtFirst checked) {
    return checkedAtFirstNames().value(int(checked));
}

CheckedAtFirst checkedAtFirstFromName(const QString& name) {
    return CheckedAtFirst(qMax(0, int(checkedAtFirstNames().indexOf(name))));
}

SpellingReviewDialog::SpellingReviewDialog(const QList<SpellingReviewItem>& items, bool inSelection,
                                           CheckedAtFirst checkedAtFirst, QWidget* parent, CodeEditSpelling* spelling)
    : QDialog(parent), m_items(items), m_spelling(spelling) {
    setWindowTitle(tr("Review Spelling"));
    resize(760, 480);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(inSelection ? tr("Misspelled words in the selection. The checked ones are corrected.")
                                         : tr("Misspelled words in the text. The checked ones are corrected."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("Checked at first:"), this));
    m_checkAtFirst = new QComboBox(this);
    m_checkAtFirst->addItem(tr("Sure corrections only"));
    m_checkAtFirst->addItem(tr("Words with a single suggestion"));
    m_checkAtFirst->addItem(tr("Every word, with its first correction"));
    m_checkAtFirst->setItemData(0, tr("A single suggestion close to the word, e.g. \"bhyc\" → być"), Qt::ToolTipRole);
    m_checkAtFirst->setItemData(1, tr("Also when it is not that close, e.g. \"agents\" → agent"), Qt::ToolTipRole);
    m_checkAtFirst->setCurrentIndex(int(checkedAtFirst));
    options->addWidget(m_checkAtFirst);
    options->addSpacing(12);
    options->addWidget(new QLabel(tr("Sort:"), this));
    auto* sort = new QComboBox(this);
    sort->addItem(tr("As in the text"), int(LineColumn));
    sort->addItem(tr("Alphabetically"), int(WordColumn));
    sort->addItem(tr("By score"), int(ScoreColumn));
    sort->setItemData(2, tr("The best (lowest) score first: how far the word is from its first correction"),
                      Qt::ToolTipRole);
    options->addWidget(sort);
    options->addStretch();
    auto* showLabel = new QLabel(tr("Sh&ow:"), this);
    options->addWidget(showLabel);
    m_show = new QComboBox(this);
    m_show->addItem(tr("All words"));
    m_show->addItem(tr("Checked words"));
    m_show->addItem(tr("Unchecked words"));
    m_show->setToolTip(tr("Checked: to look over the corrections that will be made; unchecked: those left to "
                          "decide (several suggestions, or not sure)"));
    showLabel->setBuddy(m_show);
    options->addWidget(m_show);
    layout->addLayout(options);

    m_list = new QTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Copying rows, e.g. to keep a word and its context: Ctrl+C or the
    // context menu.
    auto* copy = new QShortcut(QKeySequence::Copy, m_list, nullptr, nullptr, Qt::WidgetShortcut);
    connect(copy, &QShortcut::activated, this, [this]() { copyRows(false); });
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        if (m_list->selectedItems().isEmpty()) return;
        QMenu menu(this);
        QAction* rows = menu.addAction(tr("&Copy Rows"), this, [this]() { copyRows(false); });
        rows->setShortcut(QKeySequence::Copy);
        rows->setToolTip(tr("Word, correction, score, line and the whole line of text, separated by tabs"));
        menu.addAction(tr("Copy Con&text"), this, [this]() { copyRows(true); });
        menu.exec(m_list->viewport()->mapToGlobal(position));
    });
    m_list->setUniformRowHeights(true);
    m_list->setHeaderLabels({tr("Word"), tr("Correction"), tr("Score"), tr("Line"), tr("Context")});
    m_list->headerItem()->setToolTip(ScoreColumn, tr("How far the word is from its first correction: the lower, the "
                                                     "closer (0.3 a missing diacritic, about 1 a typing slip, more "
                                                     "for a rare word)"));
    m_list->header()->setStretchLastSection(true);
    for (int i = 0; i < m_items.size(); ++i) {
        const SpellingReviewItem& item = m_items[i];
        auto* row = new ReviewRow(m_list);
        row->setText(WordColumn, item.word.word);
        row->setData(WordColumn, kSortRole, item.word.word);
        row->setData(LineColumn, kSortRole, double(item.word.start.line) * 1e6 + item.word.start.column);
        // Without a score (no suggestion, or only a moved space): last.
        row->setData(ScoreColumn, kSortRole, item.score >= 0 ? item.score : 1e9);
        if (item.score >= 0) row->setText(ScoreColumn, QString::number(item.score, 'f', 2));
        row->setTextAlignment(ScoreColumn, Qt::AlignRight | Qt::AlignVCenter);
        row->setText(LineColumn, QString::number(item.word.start.line + 1));
        row->setTextAlignment(LineColumn, Qt::AlignRight | Qt::AlignVCenter);
        row->setText(ContextColumn, context(item.lineText, item.word.start.column, item.word.end.column));
        row->setToolTip(ContextColumn, item.lineText);
        row->setData(WordColumn, Qt::UserRole, i);
        if (item.choices.isEmpty()) {
            // Items are user-checkable by default: not this one.
            row->setText(CorrectionColumn, tr("No suggestions"));
            row->setFlags(row->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable));
            continue;
        }
        row->setCheckState(WordColumn, Qt::Unchecked);
        auto* choice = new QComboBox(m_list);
        for (const auto& correction : item.choices) choice->addItem(correction.label);
        // Picking another correction checks the word only when asked for
        // ("Picking a correction checks the word"); looking does not.
        choice->setProperty(kChosenProperty, 0);
        connect(choice, &QComboBox::activated, this, [this, row, choice](int index) {
            if (m_pickChecks->isChecked() && index != choice->property(kChosenProperty).toInt())
                row->setCheckState(WordColumn, Qt::Checked);
            choice->setProperty(kChosenProperty, index);
        });
        m_list->setItemWidget(row, CorrectionColumn, choice);
    }
    m_list->resizeColumnToContents(WordColumn);
    m_list->setColumnWidth(CorrectionColumn, qMax(160, m_list->columnWidth(WordColumn) + 40));
    m_list->resizeColumnToContents(LineColumn);
    m_list->resizeColumnToContents(ScoreColumn);
    m_list->sortItems(LineColumn, Qt::AscendingOrder);
    connect(sort, &QComboBox::activated, this, [this, sort]() {
        m_list->sortItems(sort->currentData().toInt(), Qt::AscendingOrder);
    });
    layout->addWidget(m_list, 1);
    checkAtFirst();
    connect(m_list, &QTreeWidget::itemChanged, this, &SpellingReviewDialog::updateSummary);
    connect(m_checkAtFirst, &QComboBox::activated, this, [this]() {
        checkAtFirst();
        filter();
    });
    // Applied when switched: a word unchecked while looking them over stays
    // in sight until then.
    connect(m_show, &QComboBox::activated, this, &SpellingReviewDialog::filter);

    auto* status = new QHBoxLayout;
    m_pickChecks = new QCheckBox(tr("&Picking a correction checks the word"), this);
    m_pickChecks->setToolTip(tr("Off: only the check box decides which words are corrected"));
    layout->addWidget(m_pickChecks);
    m_summary = new QLabel(this);
    status->addWidget(m_summary);
    status->addStretch();
    m_searchTime = new QLabel(this);
    status->addWidget(m_searchTime);
    layout->addLayout(status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("&Correct"));
    auto* all = buttons->addButton(tr("Check &All"), QDialogButtonBox::ActionRole);
    auto* none = buttons->addButton(tr("Check &None"), QDialogButtonBox::ActionRole);
    const auto checkAll = [this](Qt::CheckState state) {
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* row = m_list->topLevelItem(i);
            if (row->flags() & Qt::ItemIsUserCheckable) row->setCheckState(WordColumn, state);
        }
    };
    if (m_spelling) {
        auto* add = buttons->addButton(tr("Add to &Dictionary"), QDialogButtonBox::ActionRole);
        add->setToolTip(tr("The selected word is right: into the user dictionary, all its rows out of the list"));
        auto* ignore = buttons->addButton(tr("&Ignore"), QDialogButtonBox::ActionRole);
        ignore->setToolTip(tr("The selected word is right here: left as it is, all its rows out of the list"));
        connect(add, &QPushButton::clicked, this, [this]() { acceptWord(true); });
        connect(ignore, &QPushButton::clicked, this, [this]() { acceptWord(false); });
        const auto updateButtons = [this, add, ignore]() {
            const bool selected = m_list->currentItem() != nullptr;
            add->setEnabled(selected);
            ignore->setEnabled(selected);
        };
        connect(m_list, &QTreeWidget::currentItemChanged, this, updateButtons);
        updateButtons();
    }
    connect(all, &QPushButton::clicked, this, [checkAll]() { checkAll(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [checkAll]() { checkAll(Qt::Unchecked); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    updateSummary();
}

QList<QPair<SpellingReviewItem::Range, QString>> SpellingReviewDialog::chosenReplacements() const {
    QList<QPair<SpellingReviewItem::Range, QString>> result;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        if (row->checkState(WordColumn) != Qt::Checked) continue;
        const SpellingReviewItem& item = m_items[row->data(WordColumn, Qt::UserRole).toInt()];
        const auto* choice = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn));
        const int index = choice ? choice->currentIndex() : 0;
        if (index < 0 || index >= item.choices.size()) continue;
        result.append({item.choices[index].range, item.choices[index].replacement});
    }
    return result;
}

CheckedAtFirst SpellingReviewDialog::checkedAtFirst() const {
    return CheckedAtFirst(qMax(0, m_checkAtFirst->currentIndex()));
}

void SpellingReviewDialog::setSearchTime(qint64 milliseconds) {
    const double perWord = m_items.isEmpty() ? 0 : double(milliseconds) / double(m_items.size());
    m_searchTime->setText(tr("Corrections found in %1 s, %2 ms a word")
                              .arg(double(milliseconds) / 1000.0, 0, 'f', 2)
                              .arg(perWord, 0, 'f', 1));
}

void SpellingReviewDialog::checkAtFirst() {
    const CheckedAtFirst mode = checkedAtFirst();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        if (!(row->flags() & Qt::ItemIsUserCheckable)) continue;
        const SpellingReviewItem& item = m_items[row->data(WordColumn, Qt::UserRole).toInt()];
        const bool check = mode == CheckedAtFirst::Every || (mode == CheckedAtFirst::Single && item.single)
            || item.confident;
        row->setCheckState(WordColumn, check ? Qt::Checked : Qt::Unchecked);
        if (auto* choice = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn))) {
            choice->setCurrentIndex(0);
            choice->setProperty(kChosenProperty, 0);
        }
    }
}

void SpellingReviewDialog::filter() {
    const int show = m_show->currentIndex();  // all, checked, unchecked
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        const bool checked = row->checkState(WordColumn) == Qt::Checked;
        row->setHidden((show == 1 && !checked) || (show == 2 && checked));
    }
}

void SpellingReviewDialog::copyRows(bool contextOnly) const {
    QList<QTreeWidgetItem*> rows = m_list->selectedItems();
    // In the order shown, not the order selected.
    std::sort(rows.begin(), rows.end(), [this](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        return m_list->indexOfTopLevelItem(a) < m_list->indexOfTopLevelItem(b);
    });
    QStringList lines;
    for (QTreeWidgetItem* row : rows) {
        if (row->isHidden()) continue;
        const SpellingReviewItem& item = m_items[row->data(WordColumn, Qt::UserRole).toInt()];
        if (contextOnly) {
            lines << item.lineText;
            continue;
        }
        const auto* choice = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn));
        lines << QStringList{row->text(WordColumn), choice ? choice->currentText() : QString(), row->text(ScoreColumn),
                             row->text(LineColumn), item.lineText}
                     .join(QLatin1Char('\t'));
    }
    if (!lines.isEmpty()) QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

void SpellingReviewDialog::acceptWord(bool addToDictionary) {
    QTreeWidgetItem* current = m_list->currentItem();
    if (!current || !m_spelling) return;
    const QString word = current->text(WordColumn);
    if (addToDictionary) m_spelling->addToDictionary(word);
    else m_spelling->ignore(word);
    for (int i = m_list->topLevelItemCount() - 1; i >= 0; --i)
        if (m_list->topLevelItem(i)->text(WordColumn) == word) delete m_list->takeTopLevelItem(i);
    updateSummary();
}

void SpellingReviewDialog::updateSummary() {
    int checked = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->checkState(WordColumn) == Qt::Checked) ++checked;
    m_summary->setText(tr("%1 of %2 words to correct").arg(checked).arg(m_list->topLevelItemCount()));
}

int reviewSpelling(CodeEditSpelling& spelling, CheckedAtFirst& checkedAtFirst, QWidget* parent) {
    CodeEditArea* area = spelling.area();
    if (!area || !area->document() || area->readOnly()) return 0;
    const QString title = SpellingReviewDialog::tr("Review Spelling");
    if (!spelling.isEnabled() || !spelling.isReady()) {
        QMessageBox::information(parent, title,
                                 SpellingReviewDialog::tr("Spell checking is off or has no dictionary for this text."));
        return 0;
    }
    ITextDocument* doc = area->document();
    const bool inSelection = area->hasSelection();
    const int lastLine = doc->lineCount() - 1;
    const TextCursor from = inSelection ? area->selectionStart() : TextCursor{0, 0};
    const TextCursor to = inSelection ? area->selectionEnd() : TextCursor{lastLine, int(doc->lineAt(lastLine).size())};
    const auto words = spelling.misspelledWords(from, to);
    if (words.isEmpty()) {
        QMessageBox::information(parent, title,
                                 inSelection ? SpellingReviewDialog::tr("No misspelled words in the selection.")
                                             : SpellingReviewDialog::tr("No misspelled words."));
        return 0;
    }
    // Suggestions take milliseconds a word: once per distinct word, with
    // progress for long texts.
    QProgressDialog progress(SpellingReviewDialog::tr("Looking for corrections…"), SpellingReviewDialog::tr("Cancel"),
                             0, int(words.size()), parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    QElapsedTimer searching;
    searching.start();
    QHash<QString, QList<SpellChecker::Suggestion>> suggestions;
    QList<SpellingReviewItem> items;
    for (int i = 0; i < words.size(); ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) return 0;
        const auto& word = words[i];
        SpellingReviewItem item{word, doc->lineAt(word.start.line), {}, false, false};
        auto found = suggestions.find(word.word);
        if (found == suggestions.end()) found = suggestions.insert(word.word, spelling.scoredSuggestions(word.word, 3));
        for (const auto& suggestion : found.value()) item.choices.append({word, suggestion.text, suggestion.text});
        if (!found.value().isEmpty()) item.score = found.value().front().score;
        const auto phrases = spelling.phraseCorrections(word);
        for (const auto& phrase : phrases)
            item.choices.append({phrase.range, phrase.replacement, spaceCorrectionLabel(phrase.range.word, phrase.replacement)});
        item.single = phrases.isEmpty() && found.value().size() == 1;
        item.confident = item.single && SpellChecker::isConfident(word.word, found.value());
        items.append(item);
    }
    progress.setValue(int(words.size()));
    SpellingReviewDialog dialog(items, inSelection, checkedAtFirst, parent, &spelling);
    dialog.setSearchTime(searching.elapsed());
    const bool accepted = dialog.exec() == QDialog::Accepted;
    checkedAtFirst = dialog.checkedAtFirst();
    if (!accepted) return 0;
    const auto chosen = dialog.chosenReplacements();
    if (!chosen.isEmpty()) spelling.replaceWords(chosen);
    return int(chosen.size());
}

} // namespace qce::spell
