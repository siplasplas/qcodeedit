# qcodeedit — Claude Code Handoff

This document is a complete handoff for continuing development of
`qcodeedit` in Claude Code. It captures the current state, design decisions,
coding conventions, and the next planned milestones so that development can
continue without re-deriving context.

**Intended reader:** Claude Code (or any developer picking up the project).
**Maintainer:** Andrzej Borucki (`borucki.andrzej@gmail.com`).

---

## 1. What this project is

A custom Qt6 code-editor widget, to be reused across multiple projects
(DiffMerge, Gemini Commander, and others). Built because `QPlainTextEdit`'s
`protected` API and fixed-right scroll bar are limiting for use cases like
side-by-side diff views, where one pane wants its scroll bar on the left.

**Language / framework:** C++20, Qt 6.2+, CMake ≥ 3.21.
**Tests:** Qt Test (no external dependencies, consistent with DiffMerge).
**Target platforms:** Linux (Ubuntu/Debian/Fedora) and Windows (MSYS2
UCRT64). No KDE Frameworks dependency (intentional, for portability).

---

## 2. Current state (v0.2.0)

### What works
- Mono-font text rendering via `QPainter::drawText`
- Keyboard navigation: arrows, Home/End, Ctrl+Home/End, PgUp/PgDn
- Logical cursor with auto-scroll (position tracked, no visible caret yet)
- `cursorPositionChanged` signal, ready for status-bar wiring
- Configurable tab width (tabs expanded to N spaces; default 4)
- Horizontal + vertical scroll, state published via `ViewportState`
- 36 unit tests passing (5 + 12 + 19), all `QApplication`-free

### What is explicitly not in v0.2
- Visible caret (planned for v0.3)
- Margins (gutters, side bars, minimap) — v0.3
- Scroll-bar side switch (left/right) — v0.3, it's the main motivation
  for the whole project
- Selection and mouse interaction — v0.4
- Edit operations and undo/redo — v0.5
- Syntax highlighting, word-wrap, IME, search — v0.6+

---

## 3. Architecture

```
CodeEdit             (QWidget — compositional container)
└── CodeEditArea     (QAbstractScrollArea — rendering + input)
        │
        ├── LineRenderer       (internal: draws visible lines)
        ├── CursorController   (internal: pure movement logic)
        │
        ↓ reads
    ITextDocument    (pluggable backend)
        └── SimpleTextDocument   (v0.2, QStringList-based)
```

### File layout

```
qcodeedit/
├── CMakeLists.txt              # top-level, options QCE_BUILD_{DEMO,TESTS}
├── README.md                   # user-facing
├── HANDOFF.md                  # this file
├── include/qce/                # public API — consumers include from here
│   ├── CodeEdit.h
│   ├── CodeEditArea.h
│   ├── ITextDocument.h
│   ├── SimpleTextDocument.h
│   ├── TextCursor.h
│   └── ViewportState.h
├── src/                        # implementation + internal headers
│   ├── CodeEdit.cpp
│   ├── CodeEditArea.cpp
│   ├── CursorController.h / .cpp      (internal)
│   ├── ITextDocument.cpp
│   ├── LineRenderer.h / .cpp          (internal)
│   └── SimpleTextDocument.cpp
├── demo/                       # small viewer app using the library
│   ├── CMakeLists.txt
│   ├── DemoWindow.h / .cpp
│   └── main.cpp
└── tests/                      # Qt Test, no QApplication for pure logic
    ├── CMakeLists.txt
    ├── test_cursor_controller.cpp
    ├── test_simple_document.cpp
    └── test_viewport_state.cpp
```

### Design principles (non-negotiable)

These are decisions made deliberately. Don't reverse them without a strong
reason and a discussion with the maintainer.

1. **Margins don't depend on the view type.** They consume the
   `ViewportState` struct published by `CodeEditArea::viewportChanged()`.
   This allows future renderer swaps without breaking margin
   implementations. The same pattern was validated in DiffMerge.

2. **Document backend is pluggable via `ITextDocument`.** `QStringList` is
   the v0.2 backend. Gap buffer / piece table can be added later without
   touching rendering code. Don't reach into `SimpleTextDocument`-specific
   internals from the view.

3. **Internal classes stay internal.** `LineRenderer` and `CursorController`
   live in `src/`, not `include/qce/`. They are implementation details of
   `CodeEditArea`. Tests that need them set the `INTERNAL` flag in the
   `qce_add_test` CMake helper, which adds `src/` to that test's include
   path.

4. **Pure logic is Qt-widget-free where possible.** `CursorController`
   takes only an `ITextDocument*` and returns new cursor positions. No
   `QObject`, no widgets. Fully testable without a `QApplication`. Keep it
   this way.

5. **`CodeEdit` owns layout, `CodeEditArea` owns rendering.** Scroll-bar
   side, margin slots — all live in `CodeEdit`. Text rendering, cursor
   handling, keyboard input — all live in `CodeEditArea`. Don't mix.

6. **No KDE Frameworks dependency.** Only `Qt6::Core`, `Qt6::Gui`,
   `Qt6::Widgets`, `Qt6::Test`. This is a hard rule for Windows/MSYS2
   portability.

7. **Renderer choice: `QPainter::drawText`, not `QTextLayout`, for now.**
   Mono-font, line-by-line, no IME yet. Migration to `QTextLayout` is
   contained in `LineRenderer::paint()` when needed (CJK IME, word-wrap,
   rich syntax highlighting).

---

## 4. Coding conventions

### General
- **Language:** C++20. Prefer `std::unique_ptr` for ownership over raw
  `new`/`delete`. Use `enum class` over bare `enum`.
- **Comments in code:** English, always. Even when the rest of the
  conversation is in Polish.
- **DRY:** repeated code goes into helper functions. If a function is
  getting long, extract pieces into smaller helpers — don't let functions
  grow indefinitely.
- **File size:** when a file is getting hard to read, split into a new
  class. Example: `LineRenderer` was extracted from `CodeEditArea` before
  `paintEvent` grew unwieldy.

### Qt-specific
- `Q_OBJECT` macro on every class with signals/slots; CMake has global
  `AUTOMOC ON`, so no manual moc invocation needed.
- Forward-declare Qt classes in headers when possible to reduce include
  weight. (See `CodeEditArea.h` using `std::unique_ptr` with forward
  declarations.)
- Signal connections: use the type-safe pointer-to-member-function
  syntax, never string-based `SIGNAL()/SLOT()`.

### Naming
- Namespace: `qce` (short for "qcodeedit").
- Classes in CamelCase: `CodeEdit`, `CodeEditArea`, `LineRenderer`.
- Interfaces start with `I`: `ITextDocument`, `IMargin` (future).
- Member variables: `m_` prefix (`m_doc`, `m_cursor`).
- Static constants: `k` prefix in CamelCase (`kLeftPaddingPx`).
- File names match class names: `CodeEditArea.h` declares `CodeEditArea`.

### Tests
- One `.cpp` file per tested class or concept.
- Test slot names describe behavior: `moveUp_clampsColumnToShorterLine`
  rather than `testMoveUp1`.
- Use `QTEST_APPLESS_MAIN` for pure-logic tests; `QTEST_MAIN` only if a
  `QApplication` is genuinely required (widget tests).
- Add new test files via the `qce_add_test()` CMake helper in
  `tests/CMakeLists.txt`. Use the `INTERNAL` flag if the test needs access
  to headers in `src/`.

### CMake
- Options prefixed `QCE_`.
- Use target-based include directories, never global `include_directories()`.
- Library target: `qcodeedit`, aliased as `qcodeedit::qcodeedit`.
- Add new source files explicitly to `add_library(qcodeedit STATIC ...)`.
  Don't use `file(GLOB)` — it hides intent and breaks incremental builds.

---

## 5. How to build and test

```bash
cmake -B build -S .
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/demo/qcodeedit-demo README.md
```

Options:
- `-DQCE_BUILD_DEMO=OFF` — skip the demo app
- `-DQCE_BUILD_TESTS=OFF` — skip tests (useful when used as a submodule)

When used as a submodule (see section 7), typical parent projects should
set both to `OFF`.

---

## 6. v0.3 roadmap (next milestone)

Claude Code should pick up from here. The goals are:

### 6.1. Visible caret
- Add a `CaretPainter` or similar internal class (keep `CodeEditArea`
  small). It should draw a blinking vertical line at the current cursor
  position.
- Use a `QTimer` for blinking (default 500ms, configurable via
  `setCaretBlinkInterval`).
- The caret's pixel position is derived from `m_cursor` + `ViewportState`
  (line * lineHeight + ascent-ish, column * charWidth + leftPadding —
  minus `contentOffsetX`). Reuse the same math as `LineRenderer`, factor
  out into a helper if needed.
- Don't paint the caret if the editor doesn't have focus.

### 6.2. `IMargin` interface
Put in `include/qce/IMargin.h`:

```cpp
class IMargin {
public:
    virtual ~IMargin() = default;
    virtual int preferredWidth(const ViewportState& vp) const = 0;
    virtual void paint(QPainter& p, const ViewportState& vp,
                       const QRect& marginRect) = 0;
};
```

Margins **do not inherit from QWidget**. They are just drawers that the
rail widgets call. This keeps them flyweight-ish and independent of Qt's
widget lifecycle.

### 6.3. `LineNumberGutter`
First concrete `IMargin` implementation. Draws line numbers right-aligned,
using the same font as the editor. Width auto-sizes based on
`doc->lineCount()` digits. Put in `include/qce/margins/LineNumberGutter.h`.

### 6.4. `LeftRail` / `RightRail` widgets
`QWidget` subclasses that hold a list of `IMargin*` and paint them side by
side. Accept margins via `addMargin(IMargin*)`. Listen to
`CodeEditArea::viewportChanged` and schedule their own `update()`.

### 6.5. `CodeEdit::setScrollBarSide(Left|Right)`
The real implementation. On `Left`:
- Hide `CodeEditArea`'s built-in vertical scroll bar
  (`setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff)`)
- Create a standalone `QScrollBar` in `LeftRail`
- Wire it to the area's vertical scroll value (two-way)

On `Right`: default behavior, nothing extra.

### 6.6. Layout refactor for `CodeEdit`
Replace the current single-widget `QHBoxLayout` with:

```
[ LeftRail | CodeEditArea | RightRail ]
```

Each rail is optional (not added if empty). Margins are added to rails via
`codeEdit->addLeftMargin(gutter)`.

### 6.7. Tests to add
- `test_line_number_gutter.cpp` — width calculation for various line
  counts (1, 9, 10, 99, 100, 1000, …).
- Optional: `test_code_edit_area_keys.cpp` — widget-level tests using
  `QTest::keyClick`, needs `QApplication`.

---

## 7. Repository setup (GitHub + submodule)

See `REPO_SETUP.md` for the full step-by-step for initializing the Git
repository, pushing to GitHub, and consuming it as a submodule from other
projects (DiffMerge, Gemini Commander).

---

## 8. Things to watch out for

These are subtle issues discovered during v0.1/v0.2 development.

1. **`QAbstractScrollArea::paintEvent`** paints into `viewport()`, not into
   the widget itself. `QPainter p(viewport())`, not `QPainter p(this)`.

2. **`QAbstractScrollArea::viewport()->setAutoFillBackground(false)`** is
   set in the constructor — we paint the background ourselves. If you
   remove this, you'll get double-painting.

3. **`keyPressEvent` requires focus.** `setFocusPolicy(Qt::StrongFocus)`
   is set in the constructor, but the user still has to click into the
   widget before keyboard works. This is standard Qt behavior; don't work
   around it with `grabKeyboard()`.

4. **Document signal handlers must clamp the cursor.** After lines are
   removed below the cursor, or the document is reset, the cursor might
   point to a nonexistent location. Always call
   `m_cursorCtrl->clamp(m_cursor)` in `onDocumentReset`, `onLinesInserted`,
   `onLinesRemoved`.

5. **`SimpleTextDocument::setText` quirk.** `"\n"` means "one empty line",
   not "two empty lines" or "an empty document". `""` means "zero lines".
   `"abc\n"` means "one line containing abc". This is documented and
   tested — don't change it without updating both.

6. **`maxLineLength` cache invalidation.** Any mutation to
   `SimpleTextDocument::m_lines` must call `invalidateCache()`. When
   adding edit operations in v0.5, remember this.

7. **CMake `INTERNAL` flag in tests.** Tests that need `src/` headers must
   use `qce_add_test(test_name INTERNAL)`. Otherwise they'll fail to find
   `CursorController.h` or `LineRenderer.h`.

---

## 9. Open questions for later

These were deliberately deferred:

- **Grapheme clusters vs QChar index for cursor columns.** v0.2 uses
  `QChar` index. Proper grapheme-cluster awareness matters for combining
  characters (e.g., `e + ́` = `é`) and emojis. Decision: punt until
  selection/edit is in place.

- **Tab stops vs fixed-width expansion.** v0.2 uses "tab = N spaces"
  (simple replace). Proper tab stops (tab = spaces to next column that's
  a multiple of N) is more correct but requires column tracking during
  rendering. Decision: switch to tab stops in v0.3 or v0.4, whichever
  is less disruptive.

- **Smooth (sub-line) scrolling.** v0.2 scrolls in 1-line steps. Smooth
  scrolling needs `contentOffsetY` to carry the sub-line offset. Plumbing
  is there (`ViewportState::contentOffsetY`), implementation isn't.
  Decision: add when needed; not a blocker for diff view.

---

## 10. Contact

Maintainer: Andrzej Borucki, `borucki.andrzej@gmail.com`.
Preferences: Polish for conversation, English for code comments, DRY via
helpers, small focused functions, split files when they lose readability.
