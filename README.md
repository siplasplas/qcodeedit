# qcodeedit — v0.2.0

Custom Qt6 code-editor widget. Designed as a reusable component across
projects (DiffMerge, Gemini Commander, etc.) where `QPlainTextEdit`'s
`protected` API and fixed-right scroll bar are limiting.

**License:** LGPL-3.0-or-later (see `COPYING` and `COPYING.LESSER`).

**Status:** v0.2.0 — text rendering works. Read-only viewer with keyboard
navigation and a logical cursor (no visible caret yet). Margins and
scroll-bar side switch come in v0.3.

## Features (v0.2)

- Mono-font text rendering via `QPainter::drawText`
- Keyboard navigation: arrows, Home/End, Ctrl+Home/End, PgUp/PgDn
- Logical cursor with auto-scroll (position tracked, visible caret in v0.3)
- `cursorPositionChanged` signal for status-bar wiring
- Configurable tab width (default 4 spaces)
- Horizontal + vertical scroll, with state published via `ViewportState`

## Design

```
CodeEdit             (QWidget — compositional container)
└── CodeEditArea     (QAbstractScrollArea — rendering + input)
        │
        ├── LineRenderer       (internal: draws visible lines)
        ├── CursorController   (internal: pure movement logic)
        │
        ↓ reads
    ITextDocument    (pluggable backend)
        └── SimpleTextDocument   (v1, QStringList-based)
```

Key ideas:

- **Margins don't depend on the view type.** They consume a `ViewportState`
  struct published by `CodeEditArea::viewportChanged()`. Swapping the
  renderer in the future won't break margin implementations.
- **Document backend is pluggable** via `ITextDocument`. v1 uses a simple
  `QStringList`; later we can add gap buffer or piece table without touching
  the view.
- **Internal classes stay internal.** `LineRenderer` and `CursorController`
  live in `src/`, not `include/qce/`. They are implementation details of
  `CodeEditArea` — the public API doesn't expose them.
- **Pure logic is Qt-widget-free.** `CursorController` takes only an
  `ITextDocument` and returns new cursor positions. Fully testable without
  a `QApplication`.

## Build

Requires Qt 6.2+, CMake 3.21+, a C++20 compiler.

```bash
cmake -B build -S .
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/demo/qcodeedit-demo some_file.txt
```

Options:

- `QCE_BUILD_DEMO=ON` (default) — builds the demo viewer
- `QCE_BUILD_TESTS=ON` (default) — builds the Qt Test suite

## Consuming

```cmake
add_subdirectory(qcodeedit)
target_link_libraries(my_app PRIVATE qcodeedit::qcodeedit)
```

```cpp
#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/TextCursor.h>

auto* doc = new qce::SimpleTextDocument(this);
doc->setText(QStringLiteral("hello\nworld"));

auto* editor = new qce::CodeEdit(this);
editor->setDocument(doc);

// Status-bar wiring example:
connect(editor->area(), &qce::CodeEditArea::cursorPositionChanged,
        this, [this](qce::TextCursor pos) {
    statusBar()->showMessage(
        tr("Ln %1, Col %2").arg(pos.line + 1).arg(pos.column + 1));
});
```

## Testing

Three test suites, 36 test cases total:

- `test_viewport_state` — 5 (POD invariants)
- `test_simple_document` — 12 (line operations, CRLF handling, caching)
- `test_cursor_controller` — 19 (movement semantics, edge cases)

All pure logic, no `QApplication` required. Widget-level tests (painting,
key events) will come alongside v0.3 once the UI surface stabilises.

## Roadmap

- **v0.1** — skeleton (done)
- **v0.2** — text rendering, keyboard nav, logical cursor (this version)
- **v0.3** — visible caret, margins: `IMargin` interface, `LineNumberGutter`,
  `LeftRail` / `RightRail`, scroll-bar side switch
- **v0.4** — selection (still read-only document), mouse interaction
- **v0.5** — edit operations, undo/redo
- **v0.6+** — syntax highlighting, word-wrap, IME, search

## License

qcodeedit is distributed under the **GNU Lesser General Public License
version 3 or later (LGPL-3.0-or-later)**. See the files `COPYING` (GPL-3)
and `COPYING.LESSER` (LGPL-3 additional permissions) for the full license
text.

This license is consistent with Qt's own LGPL licensing. You may use
qcodeedit in proprietary applications, provided end users are able to
replace the library with their own modified version (e.g. by linking
dynamically). Modifications to qcodeedit itself must be made available
under LGPL-3.0-or-later.
