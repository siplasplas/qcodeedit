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
