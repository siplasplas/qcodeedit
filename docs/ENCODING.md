# Text file encodings (qcodeedit-encoding)

`qcodeedit::encoding` reads and writes text files in legacy code pages
(cp1250, ISO 8859-2, cp852, Mazovia, …) and in UTF-8/16/32. It is built on the
[cpg](https://github.com/siplasplas/cpg) library and is optional
(`QCE_BUILD_ENCODING`); the core editor never depends on it.

The editor always works on Unicode text. A document read from a code page
remembers it, and is saved in it again by default.

## Reading and saving

```cpp
#include <qce/encoding/Encoding.h>
#include <qce/encoding/EncodingGuard.h>

using namespace qce::encoding;

// Open: detect the encoding (or pass one, e.g. "cp1250").
const DecodeResult file = decode(bytes);
if (!file.ok) { /* file.error */ }
doc->setText(file.text);

// Guard the editor: typed, pasted and input-method text that the code page
// cannot store brings up a choice.
auto* guard = new EncodingGuard(edit->area(), this);
guard->setFormat(file.format);

// Save: in the remembered encoding, with BOM, CRLF and final line break as read.
QByteArray out;
if (guard->encodeForSave(doc->toPlainText(), &out))
    write(out);
```

`decode()` returns the text with `\n` line breaks and without BOM and final
line break; `FileFormat` keeps `encoding`, `bom`, `crlf` and `finalNewline`,
and `encode()` restores them. Without the guard, `encode()` fails and lists
the characters the encoding cannot store (or writes `?` for them when asked).

## Characters outside the code page

When text entering the editor contains characters the document's code page
cannot store, `EncodingGuard` asks:

- **Replace with '?'** — those characters are inserted as `?`;
- **Switch to UTF-8** — the text is inserted and the document is from now on
  saved as UTF-8 (`encodingChanged()` is emitted);
- **Cancel** — nothing is inserted.

The question is a message box by default; `setChoiceHandler()` replaces it
with the application's own UI. Text that bypassed the editor (e.g. a replace
through the document) is caught by `encodeForSave()` with the same choice.
`setEncoding("utf8")` switches the document to UTF-8 at any time.

## Detection

`detect(bytes, language)`:

1. ASCII-only and empty input give `utf8`.
2. UTF-8/16/32 are recognised by ICU.
3. Legacy code pages are ranked by cpg's character n-gram models over all its
   32 languages, or only `language` (e.g. `"pl"`) when given.
4. The first candidate that decodes the whole input wins; otherwise valid
   UTF-8 gives `utf8`, and anything else `fallbackEncoding()` (default
   `iso-8859-1`, which decodes any bytes and so loses nothing).

Detection is statistical: short or mixed texts can be misread, so let the user
pick another encoding (`availableEncodings()`) and decode again.

The models and `languages.txt` come from cpg (installed under
`share/cpg`, or the fetched sources); their location is fixed when qcodeedit is
built. An application that ships them elsewhere calls `setDetectionData()`.
Without the models, legacy code pages are ranked by the alphabet of the system
language only; without `languages.txt` only Unicode, ASCII and the fallback
are recognised.
