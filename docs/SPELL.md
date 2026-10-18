# Spell checking (qcodeedit-spell)

`qcodeedit::spell` is an optional companion library that checks spelling with
hunspell. Like the other parts of qcodeedit it gives mechanisms only: the
toggle, the language menu, the context menu and any dialog are the
application's.

Building it needs hunspell's development files (`libhunspell-dev` on
Debian/Ubuntu); without them the library is skipped and the rest of qcodeedit
builds as before (`QCE_BUILD_SPELL`, on by default).

```cmake
find_package(qcodeedit-spell REQUIRED)
target_link_libraries(my_app PRIVATE qcodeedit::spell)
```

## In the editor

```cpp
#include <qce/spell/CodeEditSpelling.h>

auto* spelling = new qce::spell::CodeEditSpelling(edit->area());
spelling->setLanguages({"pl"});          // or {"en", "pl"}: right in either
```

`CodeEditSpelling` underlines misspelled words with a wavy red line in the
decoration layer `CodeEditArea::SpellingLayer`, so the application's own
decorations (search matches in layer 0) stay. It checks the visible lines and
a margin around them, 150 ms after the last edit or scroll, and only the parts
`CodeEditArea::spellCheckRanges()` allows:

- without a highlighter (plain text): the whole text;
- with Kate definitions, in code: comments, strings and documentation, never
  keywords, identifiers or numbers; in markup and prose languages (Kate
  sections Markup and Other, e.g. Markdown): all text the definition does not
  exclude (`spellChecking="false"`).

Within those parts, words with digits or underscores, ALL-CAPS words,
camelCase and PascalCase identifiers, and whole tokens that look like a URL,
an e-mail address, a path, `a.b`, `a::b` or `a->b` are skipped.

For the context menu:

```cpp
if (auto word = spelling->misspelledWordAt(area->cursorPosition())) {
    for (const QString& suggestion : spelling->suggestions(word->word, 8))
        menu.addAction(suggestion, [=] { spelling->replaceWord(*word, suggestion); });  // one undo step
    menu.addAction(tr("Add to Dictionary"), [=] { spelling->addToDictionary(word->word); });
    menu.addAction(tr("Ignore"), [=] { spelling->ignore(word->word); });  // this editor, this session
}
```

`setEnabled(false)` removes the underlines; `isReady()` is false when no
dictionary for the languages is installed.

## Dictionaries and languages

```cpp
#include <qce/spell/SpellChecker.h>

qce::spell::installedDictionaries();  // name "pl_PL", language "pl", displayName "Polish (Poland)"
qce::spell::dictionaryFor("pl");      // "pl_PL"; cpg's keys work too ("cz" -> cs_CZ)
qce::spell::packageHint("de");        // "hunspell-de-de", for "install with: sudo apt install ..."
```

Dictionaries are searched in `$DICPATH`, `~/.local/share/hunspell`,
`/usr/share/hunspell`, `/usr/share/myspell` and `/usr/share/myspell/dicts`.
Only installed dictionaries are used; nothing is downloaded. A language may be
given by a dictionary name or a language code: the language's own country
comes first (`pl_PL`, `de_DE`), English is `en_US`. The languages detected by
`qce::encoding::detectLanguage()` (cpg keys) can be passed directly.

## User dictionary

**Add to Dictionary** writes the word to a user dictionary shared by all
programs using qcodeedit-spell: `~/.local/share/siplasplas/spell/<language>.dic`
(e.g. `pl.dic` for pl_PL), one word per line. A word added in one program is
known in the others: `CodeEditSpelling` takes in words added elsewhere before
each check (`SpellChecker::refreshUserDictionary()`).

`SpellChecker` can be used on its own (`check()`, `suggestions()`,
`addToDictionary()`, `ignore()`); it is not thread-safe.

## Suggestions

Checking a word is hunspell's. The suggestions come from all forms of the
dictionary: its stems with every affix rule applied (pl_PL: about 3.7 million
forms), kept in a cache file, `~/.cache/siplasplas/spell/<name>.forms`, built
in the background the first time (a few seconds), rebuilt when the dictionary
changes, and memory-mapped (shared by all programs, read on demand). Until it
is ready, and for a dictionary whose `.aff` uses what the expansion does not
implement (long or numeric flags, flag aliases, NEEDAFFIX, CIRCUMFIX,
continuation classes, e.g. French), hunspell's own suggestions are shown.

The forms are grouped by class: the letters of a word in lower case, without
Polish diacritics, with the AltGr characters of the Polish keyboard as the
letter of their key (`ð` → d, `©` → r, `ß` → t, `œ` → w), sorted. Swapped
letters, missing diacritics and a held AltGr keep the class; a letter too
many, too few or wrong is a neighbouring class. Only those classes are
ranked, by a weighted edit distance from the typed word: a missing diacritic
0.3 (a typed one removed 0.6, or 0.3 right after another such letter: AltGr
held, "żę" → "że"), two adjacent letters swapped 0.5, a missing letter 0.9,
a letter too many 1, another letter 1.5, plus 1 for a split into two words and
0.4 for a capital (a proper name) the typed word does not have. Hunspell's
suggestions are ranked the same way and break ties. Only suggestions close to
the best are shown (at most 0.6 worse), three at most (or the requested number
if smaller). On a list of 96 real typing mistakes 91 were first (hunspell
alone: 71) and the right word was always among those shown, 1.77 suggestions
on average, one for half of the words, in about 25 ms each.

Word frequency, when installed for the dictionary's language, puts common
words before rare ones: `siplasplas/spell/freq/<language>.lemma.freq` in a
data directory (`~/.local/share`, `/usr/local/share`, `/usr/share`), lines
`stem<TAB>count` of the most frequent dictionary entries after a header
`# N words in …, K known to …`, made by
[wiki2corpus](https://github.com/nlp-and-learning/wiki2corpus)'s `wordfreq`.
A suggestion's stem (hunspell's) adds 0.3 for each tenfold below 1000 per
million words; one not in the file counts as 0.3 per million (very rare); a
two-word suggestion takes its rarer word's. Only suggestions at most 0.4 worse
than the best are then shown. "dialac" gives działać (not dialach, dialag),
"reczy" rzeczy (not ręczy). With counts from film subtitles 94 of 97 typing
mistakes got the right word first (93 without) and it was always shown, 1.28
suggestions on average; counts from Wikipedia did worse, its nouns and third
person beating what people type.

A space missing or in the wrong place: `CodeEditSpelling::phraseCorrections()`
joins the misspelled word with the word before or after it on its line (also
a single letter) and splits the letters elsewhere into two right words
(`SpellChecker::splitSuggestions()`): "n aoknie" → "na oknie", "mam yz" →
"mamy z". Each correction covers both words and is applied with
`replaceWord()` as one undo step; the space moved least comes first. A split
that also needs a correction ("daj eodległosc" → "daje odległość") is not
found yet.
