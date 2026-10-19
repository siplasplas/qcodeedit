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
held, "żę" → "że"), two adjacent letters swapped 0.5 when the two hands type
them on a QWERTY keyboard and 1 when one hand does ("serach" for "search": r
and a both left), a letter two places off 1, three places 1.5 (a swap right
after another adds 0.5, so "dziłaja" gives działają before działaj; a moved
letter also lacking its diacritic adds 0.3, "dzielci" → dzielić), a missing
letter 1, a letter too many 0.9, or 0.5 when its key touches both meant
letters around it or repeats one ("bhyc": h between b and y) and 0.7 when it
touches one, another letter 1.5, for a capital inside the word that the
suggestion has not 0.35 right after the capitals the word begins with (Shift
held: "POłączyłem" → Połączyłem, which follows the word's capital; over three
letters still sure, over four not) and 1 after
a small letter (camelCase rather: "PołąCzyłem", "PoŁączyłem"), plus 1 for a
split into two words and
0.4 for a capital (a proper name) the typed word does not have. Hunspell's
suggestions are ranked the same way and break ties. Only suggestions close to
the best are shown (at most 0.6 worse), three at most (or the requested number
if smaller), and none scoring more than 2.6 (with the frequency penalty
below), so a word far from every dictionary word ("bookamrk" without an
English dictionary) has no suggestion. `SpellChecker::scoredSuggestions()`
gives the scores; `isConfident()` tells a single suggestion at most 0.8 away
and scoring at most 1 with the frequency penalty,
without a capital the typed word lacks: sure enough to apply without asking
(correction while typing, checked at first in a review). On a list of 97 real
typing mistakes 91 are first (hunspell alone: 71), 95 shown, 1.38 suggestions
on average, 39 confident and all of those right, in about 25 ms each.
A checker keeps the suggestions it found (per word) while its dictionaries,
user words, corrections or frequencies stay the same, so a second review of a
long text only looks for new words (6000 misspelled words: 45 s the first
time, under a second afterwards).
Hunspell's own suggestions take most of that time, so by default they are
asked only when the forms have no candidate: the same results on that list
(one tie broken differently elsewhere) in about 7 ms a word instead of 28;
`setHunspellSuggestions(Always)` adds them always.

Word frequency, when installed for the dictionary's language, puts common
words before rare ones: `siplasplas/spell/freq/<language>.lemma.freq` in a
data directory (`~/.local/share`, `/usr/local/share`, `/usr/share`) or else in
`~/.cache` (where downloaded files will go), lines
`stem<TAB>count` of the most frequent dictionary entries after a header
`# N words in …, K known to …`. `buildWordFrequencies()` makes it in
`~/.cache` from a list of word forms with counts, e.g.
[FrequencyWords](https://github.com/hermitdave/FrequencyWords)'
`content/2018/<language>/<language>_50k.txt` from film subtitles (CC-BY-SA
4.0; 676 KB for Polish, 0.3 s), with the installed dictionary's stems. The
full list (`_full.txt`, 19 MB) gave the same results. `qcodeedit-spelldata`'s
`WordFrequencyDownloader` fetches the list for a dictionary and makes the file
in a worker thread (about 1.5 s); the application asks the user first. The
list is kept as it is in `~/.local/share/word-frequencies/<language>_50k.txt`,
shared by programs (e.g. for detecting a text's language, by words), with a
`README.txt` naming source and licence; a checker makes the stem file again
from it in the background when the file is missing (a cleared cache) or older
than the list or the dictionary. Without
the file suggestions work as before, only rare words come up more often
("dialac": dialach, dialag, działać).
[wiki2corpus](https://github.com/nlp-and-learning/wiki2corpus)'s `wordfreq`
makes such files too, also from a text corpus.
A suggestion's stem (hunspell's) adds 0.3 for each tenfold below 1000 per
million words; one not in the file counts as 0.3 per million (very rare); a
two-word suggestion takes its rarer word's. Only suggestions at most 0.4 worse
than the best are then shown. "dialac" gives działać (not dialach, dialag).
Counts from film subtitles did better than from Wikipedia, whose nouns and
third person beat what people type.

A space in the wrong place: `CodeEditSpelling::phraseCorrections()` joins the
misspelled word with the word before or after it on its line (also a single
letter) and moves the space by one letter into two right words
(`SpellChecker::splitSuggestions()`): "n aoknie" → "na oknie", "mam yz" →
"mamy z". With word frequencies both words must be in them, a single letter
only if it is a word of its own (at least 300 per million: Polish "w", "z",
not "d" of "da sie" → "d asie"). Each correction covers both words and is
applied with `replaceWord()` as one undo step; applications offer them after
the word's own suggestions, and never apply them without asking. A split
that also needs a correction ("daj eodległosc" → "daje odległość") is not
found yet.

For reviewing a whole text or a selection, `CodeEditSpelling::misspelledWords()`
lists every misspelled word of a range (also lines never shown; unlike the
underlines, even when most of them are right in another language), and
`replaceWords()` replaces several words or phrases, each with its correction,
as one undo step.
`qce/spell/SpellingReview.h` puts this together for applications (`openSpellingReview()`
opens the window not modal, the editor read-only while it is open; `reviewSpelling()`
waits for it):
`reviewSpelling(spelling, checkedAtFirst, parent)` finds the corrections of
the selection's misspelled words (or the whole text's) and shows
`SpellingReviewDialog`: each word with its corrections, the chosen one's score
(lower is closer; sorting by it shows where sure corrections end), line and
context, sorted as in the text, alphabetically or by score,
checked at first as `CheckedAtFirst` says (sure corrections, words with a
single suggestion, or every word; the application keeps the user's choice),
"Show" all words, only the checked ones or only the unchecked ones, and how
long finding the corrections took; Ctrl+C or the context menu copies the
selected rows, separated by tabs, or their lines of text. "Add to
Dictionary" and "Ignore" take the selected word out of the list, every row of
it (an English word or a coined one written on purpose); "Remember Correction"
keeps the selected word's chosen correction as the user's own
(`SpellChecker::addCorrection()`, `~/.local/share/siplasplas/spell/<language>.corrections`,
shared by programs): from then on it is the word's only suggestion, score 0, so
sure, also while typing; "Own Correction" asks for one of the user's own and
keeps it the same way, also for a word without suggestions (which "Show" can
list alone). "User Dictionary" (`UserDictionaryDialog`) shows the
user's words and corrections and removes them. The checked words are replaced
as one undo step.

`setCorrectWhileTyping(true)` (off by default) corrects a misspelled word when
a space or punctuation is typed right after it, if its single suggestion is
confident (see above) and no moved space is offered instead. Pasted text is not corrected
(only typed keys count), nor a word right in another installed language. The
correction is its own undo step, so undo brings back the word as typed;
`corrected()` reports it, e.g. for a status bar.
