#include <qce/spell/SpellingReview.h>

#include <qce/CodeEditArea.h>
#include <qce/ITextDocument.h>
#include <qce/spell/UserDictionaryDialog.h>

#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QMenu>
#include <QShortcut>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace qce::spell {

namespace {

enum Column { WordColumn, CorrectionColumn, ScoreColumn, LineColumn, ContextColumn };

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
    static const QStringList names = {QStringLiteral("sure"), QStringLiteral("single"), QStringLiteral("every"),
                                      QStringLiteral("severalFirstClose"), QStringLiteral("several"),
                                      QStringLiteral("noSuggestions"), QStringLiteral("none")};
    return names;
}

// The line around the word, shortened at both ends.
// Where the word is in the context text, for the delegate to draw it bold.
constexpr int kBoldStartRole = Qt::UserRole + 2;
constexpr int kBoldLengthRole = Qt::UserRole + 3;

struct Context {
    QString text;
    int wordStart = 0;
};

Context context(const QString& line, int start, int end) {
    constexpr int kAround = 30;
    const int from = qMax(0, start - kAround);
    const int to = qMin(int(line.size()), end + kAround);
    // Tabs as spaces, one for one, so that the word keeps its place.
    Context result{line.mid(from, to - from).replace(QLatin1Char('\t'), QLatin1Char(' ')), start - from};
    if (from > 0) {
        result.text.prepend(QStringLiteral("…"));
        ++result.wordStart;
    }
    if (to < line.size()) result.text.append(QStringLiteral("…"));
    return result;
}

// The context with the word in bold: a text the view cannot style in part is
// drawn here as rich text.
class ContextDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear();
        QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const int start = qBound(0, index.data(kBoldStartRole).toInt(), int(text.size()));
        const int length = qBound(0, index.data(kBoldLengthRole).toInt(), int(text.size()) - start);
        const QPalette::ColorGroup group = !(opt.state & QStyle::State_Enabled) ? QPalette::Disabled
            : (opt.state & QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
        const QColor color = opt.palette.color(group, (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText
                                                                                           : QPalette::Text);
        QTextDocument document;
        document.setDefaultFont(opt.font);
        document.setDocumentMargin(0);
        document.setHtml(QStringLiteral("<span style=\"color:%1; white-space:pre\">%2<b>%3</b>%4</span>")
                             .arg(color.name(), text.left(start).toHtmlEscaped(), text.mid(start, length).toHtmlEscaped(),
                                  text.mid(start + length).toHtmlEscaped()));
        const QRect area = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        painter->save();
        painter->setClipRect(area);
        painter->translate(area.left(), area.top() + (area.height() - document.size().height()) / 2);
        document.drawContents(painter);
        painter->restore();
    }
};

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
    // The kinds of words, from the surest; the value is the CheckedAtFirst.
    const auto addKind = [this](const QString& text, CheckedAtFirst kind, const QString& tip) {
        m_checkAtFirst->addItem(text, int(kind));
        m_checkAtFirst->setItemData(m_checkAtFirst->count() - 1, tip, Qt::ToolTipRole);
    };
    addKind(tr("None (check by hand)"), CheckedAtFirst::None,
            tr("Nothing checked; every word shown, to check by hand"));
    addKind(tr("Sure corrections only"), CheckedAtFirst::Sure,
            tr("A single suggestion close to the word, e.g. \"bhyc\" → być"));
    addKind(tr("Words with a single suggestion"), CheckedAtFirst::Single,
            tr("Also when it is not that close, e.g. \"agents\" → agent"));
    addKind(tr("Several, first close"), CheckedAtFirst::SeveralFirstClose,
            tr("Only the words with several suggestions whose first is close enough to be sure if it were the only "
               "one, with that one"));
    addKind(tr("Several"), CheckedAtFirst::Several,
            tr("Only the words with several suggestions, close or not, with the first one"));
    addKind(tr("Words without suggestions"), CheckedAtFirst::NoSuggestions,
            tr("Only the words without suggestions, those given your own correction (Own Correction); sorted by "
               "score, the others are last"));
    addKind(tr("Every word, with its first correction"), CheckedAtFirst::Every, QString());
    m_checkAtFirst->setCurrentIndex(qMax(0, m_checkAtFirst->findData(int(checkedAtFirst))));
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
    m_show->addItem(tr("This kind"));
    m_show->addItem(tr("This kind, checked"));
    m_show->addItem(tr("This kind, unchecked"));
    m_show->addItem(tr("Every word"));
    m_show->setToolTip(tr("The words of the kind chosen in Checked at first: all, the checked ones (to look over "
                          "the corrections that will be made) or the unchecked ones (e.g. without suggestions: "
                          "those still without your own correction); or every word"));
    showLabel->setBuddy(m_show);
    options->addWidget(m_show);
    layout->addLayout(options);

    m_list = new QTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    // Up and Down go from row to row; Alt+Down opens the row's corrections.
    m_list->installEventFilter(this);
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
    // Rows as high as a correction box from the start: otherwise the view
    // measures the first row by its text and grows it later (e.g. when a
    // check box changes), leaving the boxes as low as they were.
    {
        QComboBox probe;
        probe.addItem(QStringLiteral("Ag"));
        m_rowHeight = qMax(probe.sizeHint().height(), m_list->fontMetrics().height() + 6);
    }
    m_list->setItemDelegateForColumn(ContextColumn, new ContextDelegate(m_list));
    m_list->setHeaderLabels({tr("Word"), tr("Correction"), tr("Score"), tr("Line"), tr("Context")});
    m_list->headerItem()->setToolTip(ScoreColumn, tr("How far the word is from its first correction: the lower, the "
                                                     "closer (0.3 a missing diacritic, about 1 a typing slip, more "
                                                     "for a rare word)"));
    m_list->header()->setStretchLastSection(true);
    for (int i = 0; i < m_items.size(); ++i) {
        const SpellingReviewItem& item = m_items[i];
        auto* row = new ReviewRow(m_list);
        row->setSizeHint(CorrectionColumn, QSize(0, m_rowHeight));
        row->setText(WordColumn, item.word.word);
        row->setData(WordColumn, kSortRole, item.word.word);
        row->setData(LineColumn, kSortRole, double(item.word.start.line) * 1e6 + item.word.start.column);
        showScore(row, item.score);
        row->setTextAlignment(ScoreColumn, Qt::AlignRight | Qt::AlignVCenter);
        row->setText(LineColumn, QString::number(item.word.start.line + 1));
        row->setTextAlignment(LineColumn, Qt::AlignRight | Qt::AlignVCenter);
        const Context shown = context(item.lineText, item.word.start.column, item.word.end.column);
        row->setText(ContextColumn, shown.text);
        row->setData(ContextColumn, kBoldStartRole, shown.wordStart);
        row->setData(ContextColumn, kBoldLengthRole, item.word.end.column - item.word.start.column);
        row->setToolTip(ContextColumn, item.lineText);
        row->setData(WordColumn, Qt::UserRole, i);
        if (item.choices.isEmpty()) {
            // Selectable (to add it to the dictionary or give it a correction)
            // but nothing to check; items are user-checkable by default.
            row->setText(CorrectionColumn, tr("No suggestions"));
            row->setForeground(CorrectionColumn, palette().brush(QPalette::Disabled, QPalette::Text));
            row->setFlags(row->flags() & ~Qt::ItemIsUserCheckable);
            continue;
        }
        row->setCheckState(WordColumn, Qt::Unchecked);
        addChoiceBox(row, i);
    }
    m_list->resizeColumnToContents(WordColumn);
    m_list->setColumnWidth(CorrectionColumn, qMax(160, m_list->columnWidth(WordColumn) + 40));
    m_list->resizeColumnToContents(LineColumn);
    m_list->resizeColumnToContents(ScoreColumn);
    m_list->sortItems(LineColumn, Qt::AscendingOrder);
    connect(sort, &QComboBox::activated, this, [this, sort]() {
        m_list->sortItems(sort->currentData().toInt(), Qt::AscendingOrder);
        showCurrent();
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
    // What to do with the selected word, and the user's own dictionary.
    auto* wordActions = new QHBoxLayout;
    if (m_spelling) {
        auto* add = new QPushButton(tr("Add to &Dictionary"), this);
        add->setToolTip(tr("The selected word is right: into the user dictionary, all its rows out of the list"));
        auto* ignore = new QPushButton(tr("&Ignore"), this);
        ignore->setToolTip(tr("The selected word is right here: left as it is, all its rows out of the list"));
        auto* remember = new QPushButton(tr("&Remember Correction"), this);
        remember->setToolTip(tr("The selected word's correction is the one to use: it becomes the word's only "
                                "correction, score 0, here and from now on (also while typing)"));
        auto* own = new QPushButton(tr("&Own Correction..."), this);
        own->setToolTip(tr("Your own correction of the selected word, also one without suggestions: remembered "
                           "as its only correction, score 0"));
        auto* userDictionary = new QPushButton(tr("&User Dictionary..."), this);
        userDictionary->setToolTip(tr("Your own words and corrections, to look over or remove"));
        for (QPushButton* button : {add, ignore, remember, own, userDictionary}) {
            button->setAutoDefault(false);
            wordActions->addWidget(button);
        }
        connect(add, &QPushButton::clicked, this, [this]() { acceptWord(true); });
        connect(ignore, &QPushButton::clicked, this, [this]() { acceptWord(false); });
        connect(remember, &QPushButton::clicked, this, &SpellingReviewDialog::rememberCorrection);
        connect(own, &QPushButton::clicked, this, &SpellingReviewDialog::ownCorrection);
        connect(userDictionary, &QPushButton::clicked, this, [this]() {
            UserDictionaryDialog(m_spelling->checker(), this).exec();
            m_spelling->recheck();
        });
        m_updateButtons = [this, add, ignore, remember, own]() {
            QTreeWidgetItem* current = m_list->currentItem();
            add->setEnabled(current);
            ignore->setEnabled(current);
            own->setEnabled(current);
            remember->setEnabled(current && m_list->itemWidget(current, CorrectionColumn));
        };
        connect(m_list, &QTreeWidget::currentItemChanged, this, m_updateButtons);
        m_updateButtons();
    }
    wordActions->addStretch();
    m_pickChecks = new QCheckBox(tr("&Picking a correction checks the word"), this);
    m_pickChecks->setToolTip(tr("Off: only the check box decides which words are corrected"));
    wordActions->addWidget(m_pickChecks);
    layout->addLayout(wordActions);
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
        {
            const QScopedValueRollback bulk(m_bulkChange, true);
            for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
                QTreeWidgetItem* row = m_list->topLevelItem(i);
                if (row->flags() & Qt::ItemIsUserCheckable) row->setCheckState(WordColumn, state);
            }
        }
        updateSummary();
    };
    connect(all, &QPushButton::clicked, this, [checkAll]() { checkAll(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [checkAll]() { checkAll(Qt::Unchecked); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    // The list shows the chosen kind from the start, not only after a change.
    filter();
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
    return CheckedAtFirst(m_checkAtFirst->currentData().toInt());
}

void SpellingReviewDialog::setSearchTime(qint64 milliseconds) {
    const double perWord = m_items.isEmpty() ? 0 : double(milliseconds) / double(m_items.size());
    m_searchTime->setText(tr("Corrections found in %1 s, %2 ms a word")
                              .arg(double(milliseconds) / 1000.0, 0, 'f', 2)
                              .arg(perWord, 0, 'f', 1));
}

void SpellingReviewDialog::checkAtFirst() {
    const CheckedAtFirst mode = checkedAtFirst();
    {
        const QScopedValueRollback bulk(m_bulkChange, true);
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* row = m_list->topLevelItem(i);
            if (!(row->flags() & Qt::ItemIsUserCheckable)) continue;
            const SpellingReviewItem& item = m_items[row->data(WordColumn, Qt::UserRole).toInt()];
            const bool check = mode != CheckedAtFirst::None && ofKind(item, mode);
            row->setCheckState(WordColumn, check ? Qt::Checked : Qt::Unchecked);
            if (auto* choice = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn)))
                choice->setCurrentIndex(0);
        }
    }
    updateSummary();
}

bool SpellingReviewDialog::ofKind(const SpellingReviewItem& item, CheckedAtFirst kind) {
    const bool several = item.choices.size() > 1;
    switch (kind) {
    case CheckedAtFirst::Sure: return item.confident;
    case CheckedAtFirst::Single: return item.single;
    case CheckedAtFirst::Every: return true;
    case CheckedAtFirst::SeveralFirstClose: return several && item.firstClose;
    case CheckedAtFirst::Several: return several;
    case CheckedAtFirst::NoSuggestions: return item.noSuggestions;
    case CheckedAtFirst::None: return true;  // every word, none checked
    }
    return false;
}

void SpellingReviewDialog::filter() {
    // The words of the kind chosen in "Checked at first": all, checked or
    // unchecked (e.g. the words without suggestions still without an own
    // correction); or every word.
    const int show = m_show->currentIndex();
    const CheckedAtFirst kind = checkedAtFirst();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        const SpellingReviewItem& item = m_items[row->data(WordColumn, Qt::UserRole).toInt()];
        const bool checked = row->checkState(WordColumn) == Qt::Checked;
        const bool hidden = show != 3
            && (!ofKind(item, kind) || (show == 1 && !checked) || (show == 2 && checked));
        row->setHidden(hidden);
    }
    showCurrent();
}

// The selected row stays in sight after sorting or filtering (if shown).
void SpellingReviewDialog::showCurrent() {
    QTreeWidgetItem* current = m_list->currentItem();
    if (current && !current->isHidden()) m_list->scrollToItem(current, QAbstractItemView::PositionAtCenter);
}

bool SpellingReviewDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_list && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Down && (key->modifiers() & Qt::AltModifier)) {
            if (QTreeWidgetItem* current = m_list->currentItem())
                if (auto* box = qobject_cast<QComboBox*>(m_list->itemWidget(current, CorrectionColumn)))
                    box->showPopup();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void SpellingReviewDialog::addChoiceBox(QTreeWidgetItem* row, int index) {
    auto* choice = new QComboBox(m_list);
    // Not in the keyboard's way: arrows move between rows, Alt+Down opens it.
    choice->setFocusPolicy(Qt::NoFocus);
    choice->setFixedHeight(m_rowHeight);
    for (const auto& correction : m_items[index].choices) choice->addItem(correction.label);
    // Picking a correction, also the one shown, checks the word only when
    // asked for ("Picking a correction checks the word").
    connect(choice, &QComboBox::activated, this, [this, row]() {
        if (m_pickChecks->isChecked()) row->setCheckState(WordColumn, Qt::Checked);
    });
    // The score follows the correction shown.
    connect(choice, &QComboBox::currentIndexChanged, this, [this, row, index](int shown) {
        const auto& choices = m_items[index].choices;
        showScore(row, shown >= 0 && shown < choices.size() ? choices[shown].score : -1);
    });
    m_list->setItemWidget(row, CorrectionColumn, choice);
}

void SpellingReviewDialog::ownCorrection() {
    QTreeWidgetItem* current = m_list->currentItem();
    if (!current || !m_spelling) return;
    const QString word = current->text(WordColumn);
    const int currentIndex = current->data(WordColumn, Qt::UserRole).toInt();
    const SpellingReviewItem& currentItem = m_items[currentIndex];
    // The word with its neighbours on the line: the text up to the space
    // before the word before it and after the word after it.
    const QString& line = currentItem.lineText;
    int from = currentItem.word.start.column, to = currentItem.word.end.column;
    while (from > 0 && line[from - 1].isSpace()) --from;
    while (from > 0 && !line[from - 1].isSpace()) --from;
    while (to < line.size() && line[to].isSpace()) ++to;
    while (to < line.size() && !line[to].isSpace()) ++to;
    const QString phrase = line.mid(from, to - from);

    QDialog ask(this);
    ask.setWindowTitle(tr("Own Correction"));
    auto* layout = new QVBoxLayout(&ask);
    auto* label = new QLabel(&ask);
    label->setWordWrap(true);
    auto* edit = new QLineEdit(word, &ask);
    // Monospaced: each letter, space and punctuation mark stands out.
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->selectAll();
    auto* withNeighbours = new QCheckBox(tr("&With the words before and after (only here, not remembered)"), &ask);
    withNeighbours->setEnabled(phrase != word);
    const auto describe = [&]() {
        label->setText(withNeighbours->isChecked()
            ? tr("Replace \"%1\" here with:").arg(phrase)
            : tr("Correct \"%1\" to (remembered: the word's only correction from now on, score 0):").arg(word));
    };
    connect(withNeighbours, &QCheckBox::toggled, &ask, [&](bool on) {
        edit->setText(on ? phrase : word);
        edit->selectAll();
        describe();
    });
    describe();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    layout->addWidget(label);
    layout->addWidget(edit);
    layout->addWidget(withNeighbours);
    layout->addWidget(buttons);
    ask.resize(qMax(ask.sizeHint().width(), 460), ask.sizeHint().height());
    if (ask.exec() != QDialog::Accepted) return;
    const QString correction = edit->text().trimmed();
    if (withNeighbours->isChecked()) {
        // This place only: a choice of this row replacing the three words.
        if (correction.isEmpty() || correction == phrase) return;
        SpellingReviewItem& item = m_items[currentIndex];
        const SpellingReviewItem::Range range{{item.word.start.line, from}, {item.word.end.line, to}, phrase};
        item.choices.prepend({range, correction, QStringLiteral("%1 → %2").arg(phrase, correction), 0});
        if (auto* box = qobject_cast<QComboBox*>(m_list->itemWidget(current, CorrectionColumn))) {
            box->insertItem(0, item.choices.front().label);
            box->setCurrentIndex(0);
        } else {
            current->setText(CorrectionColumn, QString());
            current->setFlags(current->flags() | Qt::ItemIsUserCheckable);
            addChoiceBox(current, currentIndex);
        }
        showScore(current, 0);
        current->setCheckState(WordColumn, Qt::Checked);
        updateSummary();
        if (m_updateButtons) m_updateButtons();
        return;
    }
    if (correction.isEmpty() || correction == word) return;
    m_spelling->checker().addCorrection(word, correction);
    m_bulkChange = true;
    // Every row of the word: that correction first, score 0, checked.
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        if (row->text(WordColumn) != word) continue;
        const int index = row->data(WordColumn, Qt::UserRole).toInt();
        SpellingReviewItem& item = m_items[index];
        item.choices.prepend({item.word, correction, correction, 0});
        item.confident = item.single = true;
        if (auto* sameBox = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn))) {
            sameBox->insertItem(0, correction);
            sameBox->setCurrentIndex(0);
        } else {
            row->setText(CorrectionColumn, QString());
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            addChoiceBox(row, index);
        }
        showScore(row, 0);
        row->setCheckState(WordColumn, Qt::Checked);
    }
    m_bulkChange = false;
    updateSummary();
    if (m_updateButtons) m_updateButtons();
}

void SpellingReviewDialog::showScore(QTreeWidgetItem* row, double score) {
    // Without a score (no suggestion, or a moved space): sorted last.
    row->setData(ScoreColumn, kSortRole, score >= 0 ? score : 1e9);
    row->setText(ScoreColumn, score >= 0 ? QString::number(score, 'f', 2) : QString());
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

void SpellingReviewDialog::rememberCorrection() {
    QTreeWidgetItem* current = m_list->currentItem();
    if (!current || !m_spelling) return;
    const SpellingReviewItem& item = m_items[current->data(WordColumn, Qt::UserRole).toInt()];
    const auto* box = qobject_cast<QComboBox*>(m_list->itemWidget(current, CorrectionColumn));
    if (!box || box->currentIndex() < 0 || box->currentIndex() >= item.choices.size()) return;
    const SpellingReviewItem::Choice chosen = item.choices[box->currentIndex()];
    // A moved space changes two words: not a correction of this one.
    if (!(chosen.range.start == item.word.start && chosen.range.end == item.word.end)) return;
    const QString word = item.word.word;
    m_spelling->checker().addCorrection(word, chosen.replacement);
    m_bulkChange = true;
    // Every row of the word: that correction, score 0, checked.
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        if (row->text(WordColumn) != word) continue;
        SpellingReviewItem& same = m_items[row->data(WordColumn, Qt::UserRole).toInt()];
        auto* sameBox = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn));
        if (!sameBox) continue;
        int index = -1;
        for (int k = 0; k < same.choices.size(); ++k)
            if (same.choices[k].replacement == chosen.replacement && same.choices[k].range.start == same.word.start)
                index = k;
        if (index < 0) continue;
        same.choices[index].score = 0;
        same.confident = same.single = true;
        sameBox->setCurrentIndex(index);
        showScore(row, 0);
        row->setCheckState(WordColumn, Qt::Checked);
    }
    m_bulkChange = false;
    updateSummary();
}

void SpellingReviewDialog::acceptWord(bool addToDictionary) {
    QTreeWidgetItem* current = m_list->currentItem();
    if (!current || !m_spelling) return;
    const QString word = current->text(WordColumn);
    if (addToDictionary) m_spelling->addToDictionary(word);
    else m_spelling->ignore(word);
    // The rows of the word go; the selection moves on to the next row left
    // (else the one before), to go on with the next word.
    int next = m_list->indexOfTopLevelItem(current);
    for (int i = m_list->topLevelItemCount() - 1; i >= 0; --i) {
        if (m_list->topLevelItem(i)->text(WordColumn) != word) continue;
        delete m_list->takeTopLevelItem(i);
        if (i < next) --next;
    }
    QTreeWidgetItem* following = nullptr;
    for (int i = next; i < m_list->topLevelItemCount() && !following; ++i)
        if (!m_list->topLevelItem(i)->isHidden()) following = m_list->topLevelItem(i);
    for (int i = qMin(next, m_list->topLevelItemCount()) - 1; i >= 0 && !following; --i)
        if (!m_list->topLevelItem(i)->isHidden()) following = m_list->topLevelItem(i);
    if (following) {
        m_list->setCurrentItem(following);
        m_list->scrollToItem(following);
    }
    m_list->setFocus();
    updateSummary();
    if (m_updateButtons) m_updateButtons();
}

void SpellingReviewDialog::updateSummary() {
    // Counting every row: once after a change of many rows, not for each
    // (and not before the label exists, while the window is being made).
    if (m_bulkChange || !m_summary) return;
    int checked = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->checkState(WordColumn) == Qt::Checked) ++checked;
    m_summary->setText(tr("%1 of %2 words to correct").arg(checked).arg(m_list->topLevelItemCount()));
}

namespace {

struct Review {
    QList<SpellingReviewItem> items;
    bool inSelection = false;
    qint64 milliseconds = 0;
};

// The misspelled words of the selection (or the whole text) with their
// corrections; none, after telling the user why, when there is nothing to
// review or the search was cancelled.
std::optional<Review> collectReview(CodeEditSpelling& spelling, QWidget* parent) {
    CodeEditArea* area = spelling.area();
    if (!area || !area->document() || area->readOnly()) return std::nullopt;
    const QString title = SpellingReviewDialog::tr("Review Spelling");
    if (!spelling.isEnabled() || !spelling.isReady()) {
        QMessageBox::information(parent, title,
                                 SpellingReviewDialog::tr("Spell checking is off or has no dictionary for this text."));
        return std::nullopt;
    }
    ITextDocument* doc = area->document();
    Review review;
    review.inSelection = area->hasSelection();
    const int lastLine = doc->lineCount() - 1;
    const TextCursor from = review.inSelection ? area->selectionStart() : TextCursor{0, 0};
    const TextCursor to = review.inSelection ? area->selectionEnd()
                                             : TextCursor{lastLine, int(doc->lineAt(lastLine).size())};
    const auto words = spelling.misspelledWords(from, to);
    if (words.isEmpty()) {
        QMessageBox::information(parent, title,
                                 review.inSelection ? SpellingReviewDialog::tr("No misspelled words in the selection.")
                                                    : SpellingReviewDialog::tr("No misspelled words."));
        return std::nullopt;
    }
    // Suggestions take milliseconds a word: once per distinct word, with
    // progress for long texts (blocking only the editor's window).
    QProgressDialog progress(SpellingReviewDialog::tr("Looking for corrections…"), SpellingReviewDialog::tr("Cancel"),
                             0, int(words.size()), parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    QElapsedTimer searching;
    searching.start();
    QHash<QString, QList<SpellChecker::Suggestion>> suggestions;
    for (int i = 0; i < words.size(); ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) return std::nullopt;
        const auto& word = words[i];
        SpellingReviewItem item{word, doc->lineAt(word.start.line), {}, false, false};
        auto found = suggestions.find(word.word);
        if (found == suggestions.end()) found = suggestions.insert(word.word, spelling.scoredSuggestions(word.word, 3));
        for (const auto& suggestion : found.value())
            item.choices.append({word, suggestion.text, suggestion.text, suggestion.score});
        if (!found.value().isEmpty()) item.score = found.value().front().score;
        const auto phrases = spelling.phraseCorrections(word);
        for (const auto& phrase : phrases)
            item.choices.append({phrase.range, phrase.replacement, spaceCorrectionLabel(phrase.range.word, phrase.replacement)});
        item.single = phrases.isEmpty() && found.value().size() == 1;
        item.confident = item.single && SpellChecker::isConfident(word.word, found.value());
        item.firstClose = !found.value().isEmpty() && SpellChecker::isClose(word.word, found.value().front());
        item.noSuggestions = item.choices.isEmpty();
        review.items.append(item);
    }
    progress.setValue(int(words.size()));
    review.milliseconds = searching.elapsed();
    return review;
}

// The replacements whose text is still where it was found (the document may
// have been read again from disk meanwhile).
QList<QPair<SpellingReviewItem::Range, QString>> stillThere(const ITextDocument* doc,
                                                           const QList<QPair<SpellingReviewItem::Range, QString>>& chosen) {
    QList<QPair<SpellingReviewItem::Range, QString>> result;
    for (const auto& replacement : chosen) {
        const auto& range = replacement.first;
        if (range.start.line != range.end.line || range.start.line >= doc->lineCount()) continue;
        const QString line = doc->lineAt(range.start.line);
        if (line.mid(range.start.column, range.end.column - range.start.column) == range.word) result.append(replacement);
    }
    return result;
}

} // namespace

int reviewSpelling(CodeEditSpelling& spelling, CheckedAtFirst& checkedAtFirst, QWidget* parent) {
    const auto review = collectReview(spelling, parent);
    if (!review) return 0;
    SpellingReviewDialog dialog(review->items, review->inSelection, checkedAtFirst, parent, &spelling);
    dialog.setSearchTime(review->milliseconds);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    checkedAtFirst = dialog.checkedAtFirst();
    if (!accepted || !spelling.area() || !spelling.area()->document()) return 0;
    const auto chosen = stillThere(spelling.area()->document(), dialog.chosenReplacements());
    if (!chosen.isEmpty()) spelling.replaceWords(chosen);
    return int(chosen.size());
}

SpellingReviewDialog* openSpellingReview(CodeEditSpelling& spelling, CheckedAtFirst checkedAtFirst, QWidget* parent) {
    const auto review = collectReview(spelling, parent);
    if (!review) return nullptr;
    auto* dialog = new SpellingReviewDialog(review->items, review->inSelection, checkedAtFirst, parent, &spelling);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    dialog->setSearchTime(review->milliseconds);
    // The text stays as it was found while the window is open.
    QPointer<CodeEditArea> area = spelling.area();
    const bool wasReadOnly = area->readOnly();
    area->setReadOnly(true);
    QPointer<CodeEditSpelling> checked(&spelling);
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog, area, checked, wasReadOnly](int result) {
        if (area) area->setReadOnly(wasReadOnly);
        int corrected = 0;
        if (result == QDialog::Accepted && area && area->document() && checked) {
            const auto chosen = stillThere(area->document(), dialog->chosenReplacements());
            if (!chosen.isEmpty()) checked->replaceWords(chosen);
            corrected = int(chosen.size());
        }
        emit dialog->reviewFinished(corrected, dialog->checkedAtFirst());
    });
    dialog->show();
    return dialog;
}

} // namespace qce::spell
