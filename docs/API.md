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

**Input methods:**

The area accepts text from input methods like `QPlainTextEdit`: IME
composition (fcitx5, ibus, Windows IME), compose sequences and dead keys.
Nothing needs to be enabled.

- The text being composed (pre-edit) is painted underlined at the caret, but
  it is not part of the document and not undoable. `preeditString()` returns
  it.
- Committed text follows the typing rules: it replaces the selection and, in
  overwrite mode, the character under the cursor. Together with an input
  method's replacement range it forms one undo step.
- `inputMethodQuery()` reports the caret rectangle (so the candidate window
  appears at the caret), the font, the current line as surrounding text, the
  cursor and anchor columns, and the selection within the line.
- A read-only area turns input methods off (`Qt::WA_InputMethodEnabled`).
- Focus out, `setDocument()`, `setCursorPosition()` and a mouse click end a
  pending composition.
- AltGr characters are accepted also when the platform reports AltGr as
  Ctrl+Alt (Windows); Ctrl shortcuts (Ctrl+A, Ctrl+Z, …) apply only without
  Alt.

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
area->setSelection({0, 2}, {1, 5}); // anchor, active cursor; preserves direction
area->selectAll();
area->clearSelection();
TextCursor s = area->selectionStart();
TextCursor e = area->selectionEnd();
```

---

### Additional range decorations

```cpp
#include <qce/ExtraSelection.h>

area->setExtraSelections({
    {{0, 2}, {0, 8}, QColor(230, 190, 40, 110), {}},
    {{0, 4}, {0, 7}, QColor(240, 140, 40), {}} // later entry wins
});
area->setSelection({0, 4}, {0, 7}); // active match is an editable selection
QVector<qce::ExtraSelection> ranges = area->extraSelections();
area->setExtraSelections({}); // clear decorations
```

`setSelection(anchor, cursor)` clamps both endpoints and scrolls the active
cursor into view. `selectionStart/End` return ordered endpoints; Shift navigation
retains the supplied anchor. Equal endpoints collapse the selection. It emits
`cursorPositionChanged` when the active cursor changes and `selectionChanged`
when either endpoint changes, without editing text or adding undo commands.

Extra ranges are half-open `[start, end)` in zero-based logical line and UTF-16
column coordinates. Endpoints are clamped, reversed ranges normalized and empty
ranges discarded; the getter returns this normalized snapshot in input order.
For overlaps, the last entry wins as a complete style. Invalid foreground keeps
syntax/default text color and font attributes; invalid background keeps the
underlying background. The ordinary selection takes priority over syntax and
extra backgrounds. Whole-line backgrounds remain an independent base layer.

Only actual visible text cells are decorated, including tab expansion and
wrapped rows, with horizontal scrolling applied. Multiline ranges do not fill
newline padding. Hidden fold contents and fold placeholders receive no
decorations, and painting never expands a fold. Colors belong to the caller;
use an invalid foreground to retain the current light/dark theme's text color.

The area clears all extra ranges on any text edit, reset or document replacement.
Clients recompute and supply a fresh snapshot after changes. Setting decorations
only repaints: it preserves cursor, selection, syntax/fold caches, document and
undo state. Ranges are indexed per line and overlaps resolved during replacement,
so painting visits the visible line segments rather than every search result.

The demo's **Edit → Highlight text occurrences…** (Ctrl+F) demonstrates the API
with literal, single-line matches and selects the first result. **Clear occurrence
highlights** removes the decorations. Search navigation and query/options storage
remain the application's responsibility.

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

    // Optional: also report the line's fold markers. Default: none.
    virtual void highlightLineWithFolds(const QString& line,
                                        const HighlightState& stateIn,
                                        QVector<StyleSpan>& spans,
                                        HighlightState& stateOut,
                                        QVector<FoldMarker>& folds) const;
};
```

`CodeEditArea` highlights through `highlightLineWithFolds()` and keeps the
markers per line, so a folding provider can use them instead of tokenising the
document again (see `IFoldingProvider::regionsFromLineMarkers`).
`RulesHighlighter` reports its `beginRegion`/`endRegion` markers there.

`HighlightState` is opaque (a context stack).  `StyleSpan` carries
`{start, length, attributeId}`.  `TextAttribute` carries
`{foreground, background, bold, italic, underline}`.

Attach / detach:

```cpp
area->setHighlighter(hl.get());   // non-owning; triggers full re-highlight
area->setHighlighter(nullptr);    // disable
```

Documents longer than `CodeEditArea::kSyncHighlightLines` (5000 lines) are
highlighted lazily, so large files open at once: lines are highlighted when
they are painted, the rest of the document in the background, in slices of a
few milliseconds, without blocking the event loop. Fold regions appear when
the background pass reaches the end. Edits re-highlight only what changed.

```cpp
int done = area->highlightedLineCount();   // lines from the top that are up to date
connect(area, &qce::CodeEditArea::highlightingCompleted, this, [] { /* e.g. hide a busy hint */ });
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

    // Optional fast path: build regions from the per-line fold markers the
    // editor collected while highlighting with `hl`. Return false (default)
    // to have the editor call computeRegions().
    virtual bool regionsFromLineMarkers(const IHighlighter* hl,
                                        const QVector<QVector<FoldMarker>>& markersPerLine,
                                        QVector<FoldRegion>& regions) const;
};
```

The editor recomputes regions after every edit. `computeRegions()` usually has
to tokenise the whole document; `regionsFromLineMarkers()` only pairs markers
the editor already has, which keeps typing fast in large files.
`RuleBasedFoldingProvider` implements it when `hl` is its own highlighter.

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
bool visible   = fs.isLineVisible(line);              // O(collapsed regions)
auto hidden    = fs.hiddenLineRanges();               // sorted [first, last] ranges, for whole-document passes
int  idx       = fs.regionStartingAt(line);           // binary search
```

Collapsed regions survive edits: after the regions are recomputed, a region
keeps the collapsed state of the region with the same start line and group,
following lines inserted or removed above it (`FoldState::shiftLines()`). An
edit that leaves the cursor on a hidden line (for example Enter at the end of
a collapsed header) expands the regions hiding it. A new document or a new
folding provider starts with no collapsed regions.

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

## 8. `ViewportState` — for custom margins and scroll sync

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
    int  logicalLine;         // 0-based document line
    int  startCol, endCol;    // logical column range
    bool isFirstRow;          // first visual row of this logical line?
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

## 9. Minimal text editor — quick start

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

## 10. Minimal DiffMerge pane — quick start

```cpp
// Create two symmetric panes
for (auto* [doc, edit, colors] : {leftPane, rightPane}) {
    edit->area()->setReadOnly(true);
    edit->area()->setHighlighter(sharedHighlighter.get()); // same hl, both panes
    edit->area()->setLineBackgroundProvider(colors);
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
| `IMargin` | caller | Non-owning pointer stored in `Rail` |
| `FoldState` | `CodeEditArea` | Accessed via `area()->foldState()` |
| `QUndoStack` | `CodeEditArea` | Accessed via `area()->undoStack()` |
