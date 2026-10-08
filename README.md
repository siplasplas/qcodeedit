# qcodeedit — v1.1.0

Custom Qt6 code-editor widget. Designed as a reusable component across
projects (DiffMerge, Gemini Commander, etc.) where `QPlainTextEdit`'s
`protected` API and fixed-right scroll bar are limiting.

**License:** LGPL-3.0-or-later (see `COPYING` and `COPYING.LESSER`).

## Features (v0.2)

- Mono-font text rendering via `QPainter::drawText`
- Keyboard navigation: arrows, Home/End, Ctrl+Home/End, PgUp/PgDn
- Logical cursor with auto-scroll (position tracked, visible caret in v0.3)
- `cursorPositionChanged` signal for status-bar wiring
- Configurable tab width (default 4 spaces)
- Horizontal + vertical scroll, with state published via `ViewportState`
- Row height is the font's ascent + descent plus 15% leading, and glyphs are
  always kept inside their row, so backgrounds of neighbouring rows never
  cover descenders or accents
- Default colours are black text on white, whatever the desktop theme;
  applications apply their own theme through the palette.
  `KateTheme::loadDefault()` picks Breeze Light or Breeze Dark to match the
  desktop colour scheme (the demo starts with it), Breeze Light when Breeze
  Dark is missing; when neither is downloaded it returns an invalid theme
  and the black-on-white default stays
- Gutter in Kate style: a strip slightly darker than the text background,
  muted line numbers, a separator line, and a short arrow on the
  continuation rows of wrapped lines. Colours come from
  `CodeEdit::setGutterColors()` (Kate themes: `IconBorder`, `LineNumbers`,
  `Separator` in `KateTheme`) or are derived from the palette
- Word wrap breaks lines where the Unicode line breaking rules allow it, as
  Kate does: after spaces, but also e.g. between `](` and after `/` in URLs;
  spaces at the end of a row hang there instead of starting the next row
- One word definition (`qce/WordBoundary.h`): letters, digits, combining
  marks and `_` form words, other characters are punctuation. Ctrl+Left/Right
  stop at words and punctuation runs (`obj` `.` `method` `(`),
  Ctrl+Backspace/Delete delete to the same stops, and
  `qce::words::isWholeWord()` / `wholeWordPattern()` give applications the
  matching whole-word test for search
- Large files open at once: syntax highlighting of documents over 5000 lines
  runs lazily (visible lines first, the rest in the background), and fold
  regions are rebuilt from markers collected while highlighting
- Code folding keeps collapsed blocks collapsed while you edit, and they move
  with their text when lines are added or removed above them
- Input methods (since 1.5.0): IME composition (fcitx5, ibus, Windows IME),
  compose sequences, dead keys and AltGr characters, with the candidate
  window placed at the caret (see `docs/API.md`)
- Legacy code pages (since 1.8.0, optional `qcodeedit::encoding`): files in
  cp1250, ISO 8859-2, cp852, Mazovia and other code pages, or UTF-8/16/32, are
  detected, edited as Unicode and saved back in their encoding with BOM, CRLF
  and the final line break as read. Text the code page cannot store asks
  whether to write `?` or switch the document to UTF-8. Since 1.9.0
  `decodeExact()` / `encodeExact()` convert without touching line breaks, for
  tools that keep mixed LF/CRLF themselves. Since 1.10.0 CR (classic Mac)
  line breaks are split into lines too, mixed line breaks are reported, and
  `detectLanguageAsync()` guesses the text's language in the background (see
  [docs/ENCODING.md](docs/ENCODING.md))

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
- `QCE_BUILD_KATE=ON` (default) — builds the `qcodeedit-kate` companion
  library (Kate Syntax XML reader, syntax-data version, data paths and
  `KateSyntaxIndex`). Depends on the core library.
- `QCE_BUILD_KATE_DOWNLOADER=ON` (default) — builds `qcodeedit-katedata`
  (`KateDataDownloader` + `qce-kate-fetch` tool) that fetches Kate syntax
  definitions and themes into the qcodeedit data directory. The only part
  that needs Qt Network. See [docs/KATE_DATA.md](docs/KATE_DATA.md).
- `QCE_BUILD_ENCODING=ON` (default) — builds `qcodeedit-encoding` (code pages
  and UTF, detection, the editor's encoding guard) on the
  [cpg](https://github.com/siplasplas/cpg) library. Needs ICU and zlib; an
  installed cpg 1.1 is used, otherwise it is fetched. See
  [docs/ENCODING.md](docs/ENCODING.md).

## Install

Installs the static library plus public headers plus CMake config files
so downstream projects can use `find_package(qcodeedit)`.

```bash
cmake --build build -j
sudo cmake --install build              # defaults to /usr/local
# or, for a local prefix:
cmake --install build --prefix "$HOME/.local"
```

Installed layout under `${prefix}`:

```
include/qce/*.h                            — core public headers
include/qce/margins/*.h                    — margin headers
include/qce/kate/*.h                       — companion (optional)
lib/libqcodeedit.a                         — core static library
lib/libqcodeedit-kate.a                    — companion static library
lib/libqcodeedit-katedata.a                — downloader static library
lib/libqcodeedit-encoding.a                — encodings static library
lib/cmake/qcodeedit/                       — find_package(qcodeedit)
lib/cmake/qcodeedit-kate/                  — find_package(qcodeedit-kate)
lib/cmake/qcodeedit-katedata/              — find_package(qcodeedit-katedata)
lib/cmake/qcodeedit-encoding/              — find_package(qcodeedit-encoding)
```

## Consuming

As a submodule / add_subdirectory sibling:

```cmake
add_subdirectory(qcodeedit)
target_link_libraries(my_app PRIVATE qcodeedit::qcodeedit)
```

As an installed package (after `cmake --install`):

```cmake
find_package(qcodeedit 1.1 REQUIRED)
target_link_libraries(my_app PRIVATE qcodeedit::qcodeedit)

# Optional: Kate Syntax XML reader (installs itself as a separate package).
find_package(qcodeedit-kate REQUIRED)
target_link_libraries(my_app PRIVATE qcodeedit::kate)

# Optional: downloader for Kate syntax definitions and themes (Qt Network).
find_package(qcodeedit-katedata REQUIRED)
target_link_libraries(my_app PRIVATE qcodeedit::katedata)

# Optional: code pages and UTF with detection (cpg, ICU, zlib).
find_package(qcodeedit-encoding REQUIRED)
target_link_libraries(my_app PRIVATE qcodeedit::encoding)
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

Qt Test suites covering cursor logic, wrap layout, rules highlighter,
fold state / rule-based folding provider, Kate XML reader,
Kate data paths / index / downloader (offline, via file:// URLs), and a
widget-level key-event suite. Smoke tests on real Kate definitions run when
a data set is present (`qce-kate-fetch`) and are skipped otherwise.

```bash
ctest --test-dir build --output-on-failure
```

For a manual loading test of the entire installed Kate XML data set (not a
unit test), run:

```bash
build/kate/qce-kate-check
build/kate/qce-kate-check --syntax-dir /path/to/syntax
```

The default directory comes from `qce::kate::syntaxDir()`, including the
`QCE_KATE_DATA_DIR` override. The tool attempts every XML, prints diagnostics
per file and ends with attempted / succeeded / failed counts. A file succeeds
only when the reader returns a highlighter without warnings; partially loaded
files with diagnostics count as failures. This checks loading, not the accuracy
of highlighting real source files. Exit codes: 0 = all passed, 1 = loading
failures or diagnostics, 2 = missing or empty directory. Built with
`QCE_BUILD_KATE=ON`; it does not download data or modify the syntax index.

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
