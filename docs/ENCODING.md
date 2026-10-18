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

`decode()` returns the text with `\n` line breaks and without BOM, ready for
`SimpleTextDocument::setText()`, which drops a final line break; `encode()`
takes `toPlainText()` and adds it back. `FileFormat` keeps `encoding`, `bom`,
`crlf` and `finalNewline`, so a file such as `one\r\n\r\n` is written back byte
for byte. Without the guard, `encode()` fails and lists the characters the
encoding cannot store (or writes `?` for them when asked).

## Line breaks

`decode()` turns LF, CRLF and CR (classic Mac) line breaks into `\n`.
`FileFormat` records the most frequent kind (`crlf`, `cr`, otherwise LF) and
whether the file mixed several kinds (`mixedLineBreaks`); `encode()` writes the
most frequent kind for every line. Applications show this information (e.g. in
their status bar) in their own words.

## Language

When the language of legacy files is known (e.g. a setting "files not in
UTF-8 are Polish"), pass it to `decode(bytes, {}, "pl")`: only that language's
code pages are considered, which is faster and avoids misreading the language
from a few words in comments; UTF is recognised regardless. Several languages
go as a list: `decode(bytes, {}, QStringList{"pl", "cz"})`.
`availableLanguages()` lists the languages cpg knows, for such a setting, and
`languageName("pl")` gives one name ("Polish").

`detectLanguage(bytes)` guesses the natural language with cpg's models (code
`"pl"`, name `"Polish"`). It costs some milliseconds per 100 KB, about as much
as showing the file, plus loading the models once (about 25 ms), so editors
call `detectLanguageAsync(bytes, this, callback)` after the file is shown; the
callback runs in the receiver's thread, and not at all if it was destroyed.
`preloadDetectionData()` at startup loads the models in the background, so the
first file with non-ASCII text does not wait for them.
The language is judged from the words around non-ASCII characters, so source
code with Polish comments is Polish; `Language::confidence` is its calibrated
probability.

## Exact conversion

Applications that keep line breaks themselves, such as a diff tool that
preserves mixed LF/CRLF per line, use `decodeExact()` and `encodeExact()`:
the text holds every character of the file, `\r` included, and is written
back as it is; only a BOM matching the encoding is removed and reported.
`EncodingGuard::encodeForSave(text, &bytes, true)` saves such text with the
same question about unrepresentable characters.

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

`detectEncoding(bytes, languages, threshold = 0.95)` gives the plausible
encodings, best first, each with its language and a calibrated confidence:

1. ASCII-only and empty input give one option `utf8`, confident.
2. UTF with a byte order mark is recognised and confident; valid UTF-8
   without one competes with the legacy code pages (one "ź" is little
   evidence).
3. Legacy code pages are ranked by cpg's character n-gram models over all its
   32 languages, or only `languages` (e.g. `{"pl", "cz"}`) when given.
4. The options are the confident winner alone, or the smallest set of
   distinct decodings whose confidence adds up to the threshold; every option
   decodes the whole input. `confident` is true for a single option reaching
   the threshold (about 99.9 % of those are right).

```cpp
const auto detection = qce::encoding::detectEncoding(bytes, {"pl"});
if (!detection.confident) {
    // e.g. cp1250 48 %, smc 48 %: open with options.front(), tell the user
    for (const auto& line : qce::encoding::previewEncodings(bytes, detection.options))
        ...; // line.variants: the line as each option decodes it
}
```

An editor opens the file with `options.front()` at once (`decode()` does,
and reports the detection in `DecodeResult::detection`) and lets the user
choose when it is not confident; a batch tool converting to UTF-8 refuses
uncertain files. `previewEncodings()` gives up to five lines where the options
differ, the lines with most differing bytes first; keep that order, since the
lines that speak most against the winner are often unrepresentative. The
notice and the choice dialog are the application's: qcodeedit gives only the
data. `detect(bytes, language)` returns the best option's name.

Without the detection models there is no evidence to ask about: the first code
page that decodes the input (by the alphabet of the first language, or of the
system language) is reported as one confident option with confidence 0;
otherwise valid UTF-8 gives `utf8`, and anything else `fallbackEncoding()`
(default `iso-8859-1`, which decodes any bytes and so loses nothing).

The models and `languages.txt` come from cpg (installed under
`share/cpg`, or the fetched sources); their location is fixed when qcodeedit is
built. An application that ships them elsewhere calls `setDetectionData()`.
Without the models, legacy code pages are ranked by the alphabet of the system
language only; without `languages.txt` only Unicode, ASCII and the fallback
are recognised.
