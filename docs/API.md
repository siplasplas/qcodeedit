# qcodeedit — Public API Reference

This document describes the public API of `qcodeedit` from the perspective of
two primary consumers: **a text editor application** and **DiffMerge**
(side-by-side diff viewer).  Internal classes (`LineRenderer`,
`CursorController`, `WrapLayout`) are not covered — they are implementation
details and not installed as public headers.

---

## Packages and includes

```cmake
find_package(qcodeedit 1.1 REQUIRED)
target_link_libraries(my_app PRIVATE qcodeedit::qcodeedit)

# Optional: Kate Syntax XML reader
find_package(qcodeedit-kate REQUIRED)
target_link_libraries(my_app PRIVATE qcodeedit::kate)
```

```cpp
#include <qce/CodeEdit.h>          // top-level widget
#include <qce/CodeEditArea.h>      // editing surface + signals
#include <qce/SimpleTextDocument.h>
#include <qce/TextCursor.h>
#include <qce/IHighlighter.h>
#include <qce/RulesHighlighter.h>
#include <qce/IFoldingProvider.h>
#include <qce/FoldState.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/FillerLine.h>
#include <qce/FillerState.h>
#include <qce/ViewportState.h>
#include <qce/IMargin.h>
#include <qce/margins/LineNumberGutter.h>
#include <qce/margins/FoldingGutter.h>

// kate companion (separate library)
#include <qce/kate/KateXmlReader.h>
```

All public types live in namespace `qce`.

---

## Architecture

```
CodeEdit  (QWidget — layout container)
├── LeftRail   (Rail — column of IMargin painters)
│       ├── LineNumberGutter
│       └── FoldingGutter
├── CodeEditArea  (QAbstractScrollArea — rendering + input)
└── RightRail  (Rail — column of IMargin painters)
```

`CodeEdit` is the widget you embed in a layout.  `CodeEditArea` is the piece
that renders text and handles keyboard/mouse.  Margins live in rails on either
side.

---

## 1. Core widget

### `CodeEdit`

```cpp
auto* editor = new qce::CodeEdit(parent);
editor->setDocument(doc);          // non-owning; doc must outlive editor

CodeEditArea* area = editor->area();  // direct access to editing surface
```

**Margins:**

```cpp
editor->addLeftMargin(margin);     // non-owning
editor->removeLeftMargin(margin);
editor->addRightMargin(margin);
editor->removeRightMargin(margin);
```

**Scrollbar side** (useful for a left pane in a side-by-side view):

```cpp
editor->setScrollBarSide(qce::CodeEdit::ScrollBarSide::Left);
editor->setScrollBarSide(qce::CodeEdit::ScrollBarSide::Right); // default
```

---

### `CodeEditArea`

Most runtime configuration goes through `editor->area()`.

**Document:**

```cpp
area->setDocument(doc);
ITextDocument* doc = area->document();
```

**Read-only / overwrite modes:**

```cpp
area->setReadOnly(true);
area->setReadOnly(false);           // default
bool ro = area->readOnly();
bool ov = area->overwriteMode();    // toggled with Insert key
```

**Display options:**

```cpp
area->setWordWrap(true);
area->setShowWhitespace(true);
area->setTabWidth(4);               // default
area->setSelectionColor(QColor("#A6D2FF"));
```

**Undo / redo:**

```cpp
area->undo();
area->redo();
bool ok = area->canUndo();
QUndoStack* stack = area->undoStack();  // wire to Edit menu actions
```

**Signals:**

```cpp
connect(area, &qce::CodeEditArea::cursorPositionChanged,
        this, [this](qce::TextCursor pos) {
    statusBar()->showMessage(
        tr("Ln %1  Col %2").arg(pos.line + 1).arg(pos.column + 1));
});

connect(area, &qce::CodeEditArea::selectionChanged, this, &MyWin::updateCopyAction);
connect(area, &qce::CodeEditArea::viewportChanged,
        this, [this](const qce::ViewportState& vp) { /* minimap update, etc. */ });
```

---

## 2. Document

### `ITextDocument`

Abstract interface.  Implement it to plug in a custom backend (gap buffer,
piece table, …).

```cpp
class ITextDocument : public QObject {
    // Read
    virtual int lineCount() const = 0;
    virtual QString lineAt(int index) const = 0;   // 0-based
    virtual int maxLineLength() const;              // default: linear scan

    // Write
    virtual TextCursor insertText(TextCursor pos, const QString& text) = 0;
    virtual QString    removeText(TextCursor start, TextCursor end) = 0;
    void stripTrailingWhitespace();  // not undoable; for "save with cleanup"

    // Signals
    void linesInserted(int startLine, int count);
    void linesRemoved (int startLine, int count);
    void linesChanged (int startLine, int count);
    void documentReset();
};
```

### `SimpleTextDocument`

QStringList-backed implementation.  Suitable for files up to a few thousand
lines.

```cpp
auto* doc = new qce::SimpleTextDocument(parent);
doc->setText("hello\nworld");       // replaces whole document; emits documentReset
doc->setLines({"line1", "line2"});  // same, but already split
QString all = doc->toPlainText();   // lines joined with '\n', no trailing newline
```

---

## 3. Cursor and selection

```cpp
struct TextCursor {
    int line   = 0;   // 0-based
    int column = 0;   // 0-based QChar index

    bool operator==(const TextCursor&) const;
    bool operator< (const TextCursor&) const;   // lexicographic
};
```

```cpp
TextCursor pos = area->cursorPosition();
area->setCursorPosition({line, col});

bool hasSel = area->hasSelection();
QString sel  = area->selectedText();
area->selectAll();
area->clearSelection();
TextCursor s = area->selectionStart();
TextCursor e = area->selectionEnd();
```

---

## 4. Syntax highlighting

### `IHighlighter`

```cpp
class IHighlighter {
    virtual HighlightState initialState() const = 0;
    virtual void highlightLine(const QString& line,
                               const HighlightState& stateIn,
                               QVector<StyleSpan>& spans,
                               HighlightState& stateOut) const = 0;
    virtual const QVector<TextAttribute>& attributes() const = 0;
};
```

`HighlightState` is opaque (a context stack).  `StyleSpan` carries
`{start, length, attributeId}`.  `TextAttribute` carries
`{foreground, background, bold, italic, underline}`.

Attach / detach:

```cpp
area->setHighlighter(hl.get());   // non-owning; triggers full re-highlight
area->setHighlighter(nullptr);    // disable
```

### `RulesHighlighter` — builder API

For when you want to wire up highlighting in code (C-like demo, custom rules):

```cpp
auto hl = std::make_unique<qce::RulesHighlighter>();

// 1. Attributes (returns index)
int attrKw = hl->addAttribute({QColor(0x00,0x00,0xAA), {}, /*bold*/true});
int attrStr = hl->addAttribute({QColor(0xC0,0x10,0x10)});
int attrCmt = hl->addAttribute({QColor(0x80,0x80,0x80), {}, false, true/*italic*/});

// 2. Keyword lists
int klKws = hl->addKeywordList({"keywords",
    {"if","else","for","while","return","break","continue"}, /*caseSensitive*/true});

// 3. Context stubs (name, defaultAttr, lineEndNextCtx, lineEndPopCount)
qce::HighlightContext ctxNormal{"Normal", -1, -1, 0, false, -1, {}};
qce::HighlightContext ctxStr   {"String", attrStr, -1, 0, false, -1, {}};
qce::HighlightContext ctxCmt   {"LineComment", attrCmt, 0, 1, false, -1, {}};
int normal = hl->addContext(ctxNormal);
int string = hl->addContext(ctxStr);
int lc     = hl->addContext(ctxCmt);

// 4. Rules
qce::HighlightRule r;
r.kind = qce::HighlightRule::Detect2Chars;
r.ch = '/'; r.ch1 = '/';
r.attributeId = attrCmt; r.nextContextId = lc;
hl->contextRef(normal).rules.push_back(r);

r = {}; r.kind = qce::HighlightRule::Keyword;
r.keywordListId = klKws; r.attributeId = attrKw;
hl->contextRef(normal).rules.push_back(r);

// 5. Initial context
hl->setInitialContextId(normal);

area->setHighlighter(hl.get());
```

Available rule kinds: `DetectChar`, `Detect2Chars`, `AnyChar`, `StringDetect`,
`WordDetect`, `RegExpr`, `Keyword`, `DetectSpaces`, `DetectIdentifier`, `Int`,
`Float`, `HlCStringChar`, `LineContinue`, `RangeDetect`, `IncludeRules`.

### `KateXmlReader` — load from Kate Syntax XML

```cpp
#include <qce/kate/KateXmlReader.h>   // needs qcodeedit::kate library

auto hl = KateXmlReader::load("/path/to/syntax/cpp.xml");
if (!hl) { /* parse error, already qWarning'd */ }
area->setHighlighter(hl.get());
```

Cross-language `IncludeRules` (`##OtherLanguage`) are resolved automatically
from the same directory as the loaded file.  gcc.xml uses external entities
and loads with a warning but does not crash.

---

## 5. Code folding

### Fold regions

```cpp
struct FoldRegion {
    int startLine, startColumn;
    int endLine,   endColumn;
    QString placeholder;           // shown when collapsed; default "…"
    bool collapsedByDefault;
    QString group;                 // "curly", "Comment", ...
    int depth;                     // computed by FoldState; ignore in provider
};
```

### `IFoldingProvider`

```cpp
class IFoldingProvider {
    virtual QVector<FoldRegion> computeRegions(const ITextDocument*) const = 0;
};
```

`CompositeFoldingProvider` merges several providers:

```cpp
auto comp = std::make_unique<qce::CompositeFoldingProvider>();
comp->add(std::make_unique<BraceFoldingProvider>());
comp->add(std::make_unique<IndentFoldingProvider>());
area->setFoldingProvider(comp.get());
```

### `RuleBasedFoldingProvider`

Derives regions from `beginRegion`/`endRegion` markers on `RulesHighlighter`
rules.  Requires a `RulesHighlighter` with region markers configured.

```cpp
const int rgCurly = hl->regionIdForName("curly");

qce::HighlightRule open;
open.kind = qce::HighlightRule::DetectChar; open.ch = '{';
open.beginRegionId = rgCurly;
hl->contextRef(normal).rules.push_back(open);

qce::HighlightRule close;
close.kind = qce::HighlightRule::DetectChar; close.ch = '}';
close.endRegionId = rgCurly;
hl->contextRef(normal).rules.push_back(close);

// Provider
auto fp = std::make_unique<qce::RuleBasedFoldingProvider>(hl.get());
fp->setPlaceholderFor("curly", "{…}");
area->setFoldingProvider(fp.get());
```

### `FoldState`

The editor owns a `FoldState`.  Margins read it via a const reference.

```cpp
FoldState& fs = area->foldState();
area->toggleFoldAt(line);   // toggle region starting on line; repaints
area->foldAll();
area->unfoldAll();
fs.foldToLevel(1);          // collapse top-level regions only
bool collapsed = fs.isCollapsed(regionIdx);
bool visible   = fs.isLineVisible(line);
```

---

## 6. Margins (gutters)

### `IMargin`

Implement to build custom margins (minimap, change bar, breakpoint gutter, …):

```cpp
class IMargin {
    virtual int   preferredWidth(const ViewportState& vp) const = 0;
    virtual void  paint(QPainter& p, const ViewportState& vp,
                        const QRect& rect) = 0;
    virtual void  mousePressed(const QPoint& local,
                               const ViewportState& vp,
                               const QRect& rect) {}   // optional
};
```

`ViewportState` (see §8) has everything needed to map document lines to pixel
rows without coupling to `CodeEditArea`.

### `LineNumberGutter`

```cpp
auto gutter = std::make_unique<qce::LineNumberGutter>(doc);
gutter->setFont(area->font());       // match editor font
editor->addLeftMargin(gutter.get());
```

### `FoldingGutter`

```cpp
auto fg = std::make_unique<qce::FoldingGutter>(
    &area->foldState(),
    [editor](int line) { editor->area()->toggleFoldAt(line); });
editor->addLeftMargin(fg.get());
```

Draws ▸ / ▾ arrows.  Clicking toggles the fold.

---

## 7. Per-line background colors

Used for breakpoints, diff highlights (added / removed / changed lines), etc.

```cpp
area->setLineBackgroundProvider([](int line) -> QColor {
    switch (line) {
    case 5:  return QColor("#FFE0E0");   // breakpoint
    case 10: return QColor("#D4F4DD");   // diff: added
    case 11: return QColor("#FBDADA");   // diff: removed
    default: return {};                  // invalid = default background
    }
});
```

The lambda is called once per visible line during each repaint.  For
DiffMerge, derive the color from a `DiffResult` data structure.

---

## 8. Filler lines (DiffMerge / side-by-side diff)

Filler lines are virtual rows inserted between document lines to keep two
panes vertically aligned.  They are not editable, not numbered, and cannot
hold the caret.

```cpp
struct FillerLine {
    int     beforeLine;   // insert before this 0-based document line
    int     rowCount;     // number of virtual rows
    QColor  fillColor;    // background color
    QString label;        // optional centred text on the first row
};
```

```cpp
auto* fs = new qce::FillerState();
fs->setFillers({
    {3, 4, QColor("#D4F4DD"), "added in other file"},
    {9, 2, QColor("#FBDADA"), "removed here"},
});
area->setFillerState(fs);         // non-owning

// After mutating in place:
fs->setFillers(updatedList);
area->refreshFillers();
```

To disable fillers: `area->setFillerState(nullptr)`.

**Typical DiffMerge setup for one pane:**

```cpp
// Left pane
auto* left  = new qce::CodeEdit(splitter);
left->setScrollBarSide(qce::CodeEdit::ScrollBarSide::Left);
left->area()->setReadOnly(true);
left->area()->setLineBackgroundProvider(leftDiffColors);
left->area()->setFillerState(leftFillers);

// Right pane
auto* right = new qce::CodeEdit(splitter);
right->area()->setReadOnly(true);
right->area()->setLineBackgroundProvider(rightDiffColors);
right->area()->setFillerState(rightFillers);
```

Scroll synchronization: connect `viewportChanged` from one pane and call
`area()->verticalScrollBar()->setValue(...)` on the other.

---

## 9. `ViewportState` — for custom margins and scroll sync

Published by `CodeEditArea::viewportChanged(const ViewportState&)`.

```cpp
struct ViewportState {
    // Geometry
    int viewportWidth, viewportHeight;  // pixels
    int charWidth, lineHeight;          // pixels (monofont)
    int contentOffsetX, contentOffsetY; // scroll offset of top-left line

    // Visible range
    int firstVisibleLine, lastVisibleLine;   // 0-based document lines
    int firstVisibleRow,  lastVisibleRow;    // visual rows (wrap-aware)

    // Per-visual-row detail (populated when wordWrap == true)
    bool wordWrap;
    QVector<RowInfo> rows;   // element 0 = firstVisibleRow

    bool isValid() const;
    int  visibleLineCount() const;
};

struct RowInfo {
    int  logicalLine;         // -1 when isFiller
    int  startCol, endCol;    // logical column range
    bool isFirstRow;          // first visual row of this logical line?
    bool isFiller;
    QColor  fillerColor;
    QString fillerLabel;
    QString foldPlaceholder;  // non-empty → collapsed fold header row
    int     foldStartColumn;
};
```

**Mapping a document line to a Y pixel in a margin:**

```cpp
void MyMargin::paint(QPainter& p, const ViewportState& vp, const QRect& rect) {
    for (int li = vp.firstVisibleLine; li <= vp.lastVisibleLine; ++li) {
        if (!area->foldState().isLineVisible(li)) continue;
        const int row    = /* compute from vp.rows or direct formula */;
        const int y      = rect.top() + (row - vp.firstVisibleRow) * vp.lineHeight
                           + vp.contentOffsetY;
        // draw at y
    }
}
```

For margins that do not support word-wrap, `firstVisibleLine`/`lastVisibleLine`
and `contentOffsetY`/`lineHeight` are sufficient.

---

## 10. Minimal text editor — quick start

```cpp
// main window setup
m_doc  = new qce::SimpleTextDocument(this);
m_edit = new qce::CodeEdit(this);
m_edit->setDocument(m_doc);

// line numbers
m_lineNumbers = std::make_unique<qce::LineNumberGutter>(m_doc);
m_lineNumbers->setFont(m_edit->area()->font());
m_edit->addLeftMargin(m_lineNumbers.get());

// Kate syntax highlighting (load on file open)
void MyEditor::loadSyntax(const QString& xmlPath) {
    m_hl = KateXmlReader::load(xmlPath);
    m_edit->area()->setHighlighter(m_hl.get());

    m_foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(m_hl.get());
    m_edit->area()->setFoldingProvider(m_foldProvider.get());
}

// fold gutter (add after folding provider is known)
m_foldGutter = std::make_unique<qce::FoldingGutter>(
    &m_edit->area()->foldState(),
    [this](int l) { m_edit->area()->toggleFoldAt(l); });
m_edit->addLeftMargin(m_foldGutter.get());

// status bar
connect(m_edit->area(), &qce::CodeEditArea::cursorPositionChanged,
        this, [this](qce::TextCursor c) {
    m_statusBar->showMessage(tr("Ln %1  Col %2").arg(c.line+1).arg(c.column+1));
});

// load file
m_doc->setText(file.readAll());
```

---

## 11. Minimal DiffMerge pane — quick start

```cpp
// Create two symmetric panes
for (auto* [doc, edit, colors, fillers] : {leftPane, rightPane}) {
    edit->area()->setReadOnly(true);
    edit->area()->setHighlighter(sharedHighlighter.get()); // same hl, both panes
    edit->area()->setLineBackgroundProvider(colors);
    edit->area()->setFillerState(fillers);
    edit->addLeftMargin(new qce::LineNumberGutter(doc));
}
leftPane.edit->setScrollBarSide(qce::CodeEdit::ScrollBarSide::Left);

// Synchronized scrolling
connect(left->area(), &qce::CodeEditArea::viewportChanged,
        this, [right](const qce::ViewportState& vp) {
    right->area()->verticalScrollBar()->setValue(vp.firstVisibleRow);
});
connect(right->area(), &qce::CodeEditArea::viewportChanged,
        this, [left](const qce::ViewportState& vp) {
    left->area()->verticalScrollBar()->setValue(vp.firstVisibleRow);
});
```

---

## Lifetime rules

| Object | Owner | Note |
|--------|-------|-------|
| `ITextDocument` | caller | Must outlive all editors using it |
| `IHighlighter` | caller | Non-owning pointer stored in `CodeEditArea` |
| `IFoldingProvider` | caller | Non-owning pointer stored in `CodeEditArea` |
| `FillerState` | caller | Non-owning pointer stored in `CodeEditArea` |
| `IMargin` | caller | Non-owning pointer stored in `Rail` |
| `FoldState` | `CodeEditArea` | Accessed via `area()->foldState()` |
| `QUndoStack` | `CodeEditArea` | Accessed via `area()->undoStack()` |
