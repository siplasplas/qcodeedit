#include <qce/spell/SpellingReview.h>

#include <qce/CodeEditArea.h>
#include <qce/ITextDocument.h>

#include <QCheckBox>
#include <QComboBox>
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

namespace qce::spell {

namespace {

enum Column { WordColumn, CorrectionColumn, LineColumn, ContextColumn };

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
                                           CheckedAtFirst checkedAtFirst, QWidget* parent)
    : QDialog(parent), m_items(items) {
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
    options->addStretch();
    m_onlyChecked = new QCheckBox(tr("Show &only checked words"), this);
    m_onlyChecked->setToolTip(tr("Hides the unchecked words, to look over the corrections that will be made"));
    options->addWidget(m_onlyChecked);
    layout->addLayout(options);

    m_list = new QTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setHeaderLabels({tr("Word"), tr("Correction"), tr("Line"), tr("Context")});
    m_list->header()->setStretchLastSection(true);
    for (int i = 0; i < m_items.size(); ++i) {
        const SpellingReviewItem& item = m_items[i];
        auto* row = new QTreeWidgetItem(m_list);
        row->setText(WordColumn, item.word.word);
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
        // Picking a correction means correcting the word.
        connect(choice, &QComboBox::activated, this, [row]() { row->setCheckState(WordColumn, Qt::Checked); });
        m_list->setItemWidget(row, CorrectionColumn, choice);
    }
    m_list->resizeColumnToContents(WordColumn);
    m_list->setColumnWidth(CorrectionColumn, qMax(160, m_list->columnWidth(WordColumn) + 40));
    m_list->resizeColumnToContents(LineColumn);
    layout->addWidget(m_list, 1);
    checkAtFirst();
    connect(m_list, &QTreeWidget::itemChanged, this, &SpellingReviewDialog::updateSummary);
    connect(m_checkAtFirst, &QComboBox::activated, this, [this]() {
        checkAtFirst();
        filter();
    });
    // Applied when switched: a word unchecked while looking them over stays
    // in sight until then.
    connect(m_onlyChecked, &QCheckBox::toggled, this, &SpellingReviewDialog::filter);

    auto* status = new QHBoxLayout;
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
        if (auto* choice = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn)))
            choice->setCurrentIndex(0);
    }
}

void SpellingReviewDialog::filter() {
    const bool only = m_onlyChecked->isChecked();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        row->setHidden(only && row->checkState(WordColumn) != Qt::Checked);
    }
}

void SpellingReviewDialog::updateSummary() {
    int checked = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->checkState(WordColumn) == Qt::Checked) ++checked;
    m_summary->setText(tr("%1 of %2 words to correct").arg(checked).arg(m_items.size()));
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
        const auto phrases = spelling.phraseCorrections(word);
        for (const auto& phrase : phrases)
            item.choices.append({phrase.range, phrase.replacement, spaceCorrectionLabel(phrase.range.word, phrase.replacement)});
        item.single = phrases.isEmpty() && found.value().size() == 1;
        item.confident = item.single && SpellChecker::isConfident(word.word, found.value());
        items.append(item);
    }
    progress.setValue(int(words.size()));
    SpellingReviewDialog dialog(items, inSelection, checkedAtFirst, parent);
    dialog.setSearchTime(searching.elapsed());
    const bool accepted = dialog.exec() == QDialog::Accepted;
    checkedAtFirst = dialog.checkedAtFirst();
    if (!accepted) return 0;
    const auto chosen = dialog.chosenReplacements();
    if (!chosen.isEmpty()) spelling.replaceWords(chosen);
    return int(chosen.size());
}

} // namespace qce::spell
