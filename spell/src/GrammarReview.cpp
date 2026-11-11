#include <qce/spell/GrammarReview.h>

#include <qce/CodeEditArea.h>
#include <qce/ITextDocument.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace qce::spell {

namespace {

enum Column { WordsColumn, CorrectionColumn, ProblemColumn, LineColumn, ContextColumn };

// The index into the mistakes, kept in the first column.
constexpr int kIndexRole = Qt::UserRole;

// A rule's name in the "Show" box.
QString ruleName(const QString& rule) {
    if (rule == QLatin1String("government")) return GrammarReviewDialog::tr("After prepositions");
    if (rule == QLatin1String("agreement")) return GrammarReviewDialog::tr("Adjective and noun");
    if (rule == QLatin1String("verbs")) return GrammarReviewDialog::tr("Two verbs");
    if (rule == QLatin1String("rare word")) return GrammarReviewDialog::tr("Rare words");
    return rule;
}

// "ze → że": a correction as listed.
QString correctionLabel(const QPair<CodeEditSpelling::Misspelling, QString>& correction) {
    return QStringLiteral("%1 → %2").arg(correction.first.word, correction.second);
}

} // namespace

struct GrammarReviewDialog::Private {
    QPointer<CodeEditSpelling> m_spelling;
    int m_firstLine = 0;
    int m_lastLine = 0;
    QList<CodeEditSpelling::GrammarMistake> m_mistakes;
    QTreeWidget* m_list = nullptr;
    QComboBox* m_filter = nullptr;
    QLabel* m_summary = nullptr;
    int m_corrected = 0;
};

GrammarReviewDialog::GrammarReviewDialog(CodeEditSpelling& spelling, int firstLine, int lastLine, bool inSelection,
                                         QWidget* parent)
    :
    QDialog(parent),
      d(std::make_unique<Private>()) {
    d->m_spelling = &spelling;
    d->m_firstLine = firstLine;
    d->m_lastLine = lastLine;
    setWindowTitle(tr("Review Grammar"));
    resize(820, 460);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(inSelection ? tr("Grammar mistakes in the lines of the selection whose words are spelled "
                                              "right. The checked mistakes shown are corrected.")
                                         : tr("Grammar mistakes in the text whose words are spelled right. The checked "
                                              "mistakes shown are corrected."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* options = new QHBoxLayout;
    auto* showLabel = new QLabel(tr("Sh&ow:"), this);
    options->addWidget(showLabel);
    d->m_filter = new QComboBox(this);
    showLabel->setBuddy(d->m_filter);
    options->addWidget(d->m_filter);
    options->addStretch();
    auto* checkAll = new QPushButton(tr("Check &All"), this);
    auto* uncheckAll = new QPushButton(tr("&Uncheck All"), this);
    for (QPushButton* button : {checkAll, uncheckAll}) {
        button->setAutoDefault(false);
        options->addWidget(button);
    }
    layout->addLayout(options);

    d->m_list = new QTreeWidget(this);
    d->m_list->setRootIsDecorated(false);
    d->m_list->setHeaderLabels({tr("Words"), tr("Correction"), tr("Problem"), tr("Line"), tr("Context")});
    d->m_list->header()->setStretchLastSection(true);
    d->m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(d->m_list, 1);
    // The selected rows to the clipboard: Ctrl+C or the context menu.
    auto* copy = new QShortcut(QKeySequence::Copy, d->m_list, nullptr, nullptr, Qt::WidgetShortcut);
    connect(copy, &QShortcut::activated, this, &GrammarReviewDialog::copyRows);
    d->m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(d->m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        if (d->m_list->selectedItems().isEmpty()) return;
        QMenu menu(this);
        QAction* rows = menu.addAction(tr("&Copy Rows"), this, &GrammarReviewDialog::copyRows);
        rows->setShortcut(QKeySequence::Copy);
        rows->setToolTip(tr("Words, correction, problem, line and the whole line of text, separated by tabs"));
        menu.exec(d->m_list->viewport()->mapToGlobal(position));
    });

    d->m_summary = new QLabel(this);
    layout->addWidget(d->m_summary);
    auto* buttons = new QDialogButtonBox(this);
    auto* correctButton = buttons->addButton(tr("&Correct"), QDialogButtonBox::ApplyRole);
    correctButton->setToolTip(tr("Corrects the checked mistakes shown; the window stays open"));
    auto* correctAndClose = buttons->addButton(tr("Correct and C&lose"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    const auto checkShown = [this](bool checked) {
        for (int i = 0; i < d->m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* row = d->m_list->topLevelItem(i);
            if (!row->isHidden() && (row->flags() & Qt::ItemIsUserCheckable))
                row->setCheckState(WordsColumn, checked ? Qt::Checked : Qt::Unchecked);
        }
    };
    connect(checkAll, &QPushButton::clicked, this, [checkShown]() { checkShown(true); });
    connect(uncheckAll, &QPushButton::clicked, this, [checkShown]() { checkShown(false); });
    connect(d->m_filter, &QComboBox::activated, this, &GrammarReviewDialog::filter);
    connect(d->m_list, &QTreeWidget::itemChanged, this, &GrammarReviewDialog::updateSummary);
    connect(correctButton, &QPushButton::clicked, this, &GrammarReviewDialog::correct);
    connect(correctAndClose, &QPushButton::clicked, this, [this]() {
        correct();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // A double click shows the words in the text (selected, scrolled into sight).
    connect(d->m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* row) {
        if (!row || !d->m_spelling || !d->m_spelling->area()) return;
        const auto& mistake = d->m_mistakes[row->data(WordsColumn, kIndexRole).toInt()];
        d->m_spelling->area()->setSelection(mistake.start, mistake.end);
    });
    collect();
}

void GrammarReviewDialog::collect() {
    d->m_mistakes.clear();
    d->m_list->clear();
    CodeEditArea* area = d->m_spelling ? d->m_spelling->area() : nullptr;
    if (!area || !area->document()) return;
    ITextDocument* doc = area->document();
    d->m_lastLine = qMin(d->m_lastLine, doc->lineCount() - 1);
    d->m_mistakes = d->m_spelling->grammarMistakes({d->m_firstLine, 0}, {d->m_lastLine, int(doc->lineAt(d->m_lastLine).size())});
    QSignalBlocker blocker(d->m_list);
    QSet<QString> rules;
    for (int i = 0; i < d->m_mistakes.size(); ++i) {
        const auto& mistake = d->m_mistakes[i];
        rules.insert(mistake.issue.rule);
        const QString line = doc->lineAt(mistake.start.line);
        auto* row = new QTreeWidgetItem(d->m_list);
        row->setData(WordsColumn, kIndexRole, i);
        row->setText(WordsColumn, line.mid(mistake.start.column, mistake.end.column - mistake.start.column));
        row->setText(ProblemColumn, mistake.issue.message.isEmpty() ? ruleName(mistake.issue.rule) : mistake.issue.message);
        row->setText(LineColumn, QString::number(mistake.start.line + 1));
        row->setTextAlignment(LineColumn, Qt::AlignRight | Qt::AlignVCenter);
        row->setText(ContextColumn, line.trimmed());
        row->setToolTip(ContextColumn, line);
        if (mistake.corrections.isEmpty()) {
            row->setText(CorrectionColumn, tr("No correction"));
            row->setFlags(row->flags() & ~Qt::ItemIsUserCheckable);
            continue;
        }
        row->setCheckState(WordsColumn, Qt::Unchecked);
        if (mistake.corrections.size() == 1) {
            row->setText(CorrectionColumn, correctionLabel(mistake.corrections.front()));
            continue;
        }
        auto* box = new QComboBox(d->m_list);
        for (const auto& correction : mistake.corrections) box->addItem(correctionLabel(correction));
        d->m_list->setItemWidget(row, CorrectionColumn, box);
    }
    // "Show": all, or the rules found.
    const QString shown = d->m_filter->currentData().toString();
    d->m_filter->clear();
    d->m_filter->addItem(tr("All mistakes"), QString());
    QStringList sorted(rules.begin(), rules.end());
    sorted.sort();
    for (const QString& rule : sorted) d->m_filter->addItem(ruleName(rule), rule);
    d->m_filter->setCurrentIndex(qMax(0, d->m_filter->findData(shown)));
    for (int column : {WordsColumn, CorrectionColumn, ProblemColumn, LineColumn}) d->m_list->resizeColumnToContents(column);
    d->m_list->setColumnWidth(CorrectionColumn, qMax(d->m_list->columnWidth(CorrectionColumn), 140));
    filter();
}

void GrammarReviewDialog::copyRows() {
    CodeEditArea* area = d->m_spelling ? d->m_spelling->area() : nullptr;
    QStringList lines;
    for (int i = 0; i < d->m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = d->m_list->topLevelItem(i);
        if (row->isHidden() || !row->isSelected()) continue;
        const auto& mistake = d->m_mistakes[row->data(WordsColumn, kIndexRole).toInt()];
        QString correction = row->text(CorrectionColumn);
        if (auto* box = qobject_cast<QComboBox*>(d->m_list->itemWidget(row, CorrectionColumn))) correction = box->currentText();
        const QString text = area && area->document() ? area->document()->lineAt(mistake.start.line) : QString();
        lines << QStringList{row->text(WordsColumn), correction, row->text(ProblemColumn), row->text(LineColumn), text}
                     .join(QLatin1Char('\t'));
    }
    if (!lines.isEmpty()) QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

void GrammarReviewDialog::filter() {
    const QString rule = d->m_filter->currentData().toString();
    for (int i = 0; i < d->m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = d->m_list->topLevelItem(i);
        const auto& mistake = d->m_mistakes[row->data(WordsColumn, kIndexRole).toInt()];
        row->setHidden(!rule.isEmpty() && mistake.issue.rule != rule);
    }
    updateSummary();
}

void GrammarReviewDialog::updateSummary() {
    int shown = 0, checked = 0;
    for (int i = 0; i < d->m_list->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* row = d->m_list->topLevelItem(i);
        if (row->isHidden()) continue;
        ++shown;
        checked += row->checkState(WordsColumn) == Qt::Checked;
    }
    d->m_summary->setText(tr("%1 of %2 mistakes shown, %3 checked").arg(shown).arg(d->m_mistakes.size()).arg(checked));
}

void GrammarReviewDialog::correct() {
    if (!d->m_spelling || !d->m_spelling->area()) return;
    QList<QPair<CodeEditSpelling::Misspelling, QString>> chosen;
    for (int i = 0; i < d->m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = d->m_list->topLevelItem(i);
        if (row->isHidden() || row->checkState(WordsColumn) != Qt::Checked) continue;
        const auto& mistake = d->m_mistakes[row->data(WordsColumn, kIndexRole).toInt()];
        int choice = 0;
        if (auto* box = qobject_cast<QComboBox*>(d->m_list->itemWidget(row, CorrectionColumn))) choice = box->currentIndex();
        if (choice >= 0 && choice < mistake.corrections.size()) chosen.append(mistake.corrections[choice]);
    }
    if (chosen.isEmpty()) return;
    // The editor is read-only while the window is open; corrections go in.
    CodeEditArea* area = d->m_spelling->area();
    const bool wasReadOnly = area->readOnly();
    area->setReadOnly(false);
    d->m_spelling->replaceWords(chosen);
    area->setReadOnly(wasReadOnly);
    d->m_corrected += int(chosen.size());
    collect();
}

GrammarReviewDialog* openGrammarReview(CodeEditSpelling& spelling, QWidget* parent) {
    CodeEditArea* area = spelling.area();
    if (!area || !area->document() || area->readOnly()) return nullptr;
    const QString title = GrammarReviewDialog::tr("Review Grammar");
    if (!spelling.isEnabled() || !spelling.isReady() || !spelling.isGrammarEnabled() || !spelling.grammarChecker()) {
        QMessageBox::information(parent, title,
                                 GrammarReviewDialog::tr("Grammar checking is off or has no data for this language."));
        return nullptr;
    }
    ITextDocument* doc = area->document();
    const bool inSelection = area->hasSelection();
    const int first = inSelection ? area->selectionStart().line : 0;
    const int last = inSelection ? area->selectionEnd().line : doc->lineCount() - 1;
    auto* dialog = new GrammarReviewDialog(spelling, first, last, inSelection, parent);
    if (dialog->mistakeCount() == 0) {
        delete dialog;
        QMessageBox::information(parent, title,
                                 inSelection ? GrammarReviewDialog::tr("No grammar mistakes in the selection.")
                                             : GrammarReviewDialog::tr("No grammar mistakes."));
        return nullptr;
    }
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    QPointer<CodeEditArea> guarded = area;
    const bool wasReadOnly = area->readOnly();
    area->setReadOnly(true);
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog, guarded, wasReadOnly]() {
        if (guarded) guarded->setReadOnly(wasReadOnly);
        emit dialog->reviewFinished(dialog->correctedSoFar());
    });
    dialog->show();
    return dialog;
}


GrammarReviewDialog::~GrammarReviewDialog() = default;
int GrammarReviewDialog::correctedSoFar() const { return d->m_corrected; }
int GrammarReviewDialog::mistakeCount() const { return int(d->m_mistakes.size()); }

} // namespace qce::spell
