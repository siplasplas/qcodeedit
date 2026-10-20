# Spell checking (qcodeedit-spell)

`qcodeedit::spell` is an optional companion library that checks spelling with
hunspell. Like the other parts of qcodeedit it gives mechanisms only: the
toggle, the language menu, the context menu and any dialog are the
application's.

Building it needs the development files of hunspell and SQLite
(`libhunspell-dev`, `libsqlite3-dev` on Debian/Ubuntu); without them the library is skipped and the rest of qcodeedit
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

### Grammar

Grammar comes through the interface `qce::spell::GrammarChecker`
(`GrammarChecker.h`): the language it checks and the mistakes of a piece of
text, each with the words in question, the rule, a message and replacements.
Built with `qcodeedit-grammar` (see [GRAMMAR.md](GRAMMAR.md)), qcodeedit-spell
has one of its own, `builtinGrammarChecker()`, used by itself when its data
files are in `grammarDataDir()` (on Linux
`~/.local/share/siplasplas/spell/grammar`): `pl.morph` is needed,
`pl.forms.tsv` and `pl.complements.tsv` improve the rules, and the rules file
`pl.grammar` installed with qcodeedit-grammar is used unless one is there.
The spellmorf tools make the files. A program may give its own checker
instead, or turn grammar off:

```cpp
spelling->setGrammarChecker(std::make_shared<MyGrammar>());
spelling->setGrammarEnabled(false);
```

When a dictionary of the checker's language is in use, mistakes whose
words are all spelled right are underlined in their own color (blue by
default, `setGrammarUnderlineColor()`) in `CodeEditArea::GrammarLayer`:
spelling comes first, a misspelled word keeps its red line only. Not in the
word being typed, nor in a text found to be in another language. For the
menu:

```cpp
if (auto mistake = spelling->grammarMistakeAt(area->cursorPosition()))
    for (const auto& [range, text] : mistake->corrections)
        menu.addAction(text, [=] { spelling->replaceWord(range, text); });
```

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

The user's own spelling data is one SQLite database shared by all programs
using qcodeedit-spell: `~/.local/share/siplasplas/spell/spelling.sqlite`
(`userDataPath()`). It holds the user dictionary and the own corrections of
each language (`pl` for pl_PL), and the words ignored in each file, by its
path. **Add to Dictionary** writes the word there; a word added in one program
is known in the others: `CodeEditSpelling` takes in changes made elsewhere
before each check (`SpellChecker::refreshUserDictionary()`). A word of the
user's is right whatever hunspell says (hunspell rejects an added "Kazaa"
when the dictionary has "KaZaA"); one added in lower case is right also
capitalised or in capitals, and replaces those spellings added before
("kazaa" after "Kazaa"), while they add nothing to it. User dictionaries
of earlier versions (`<language>.dic` beside the database) are taken in once
and moved to its `imported` directory.

**Ignore** keeps the word for the document's file when the application sets
it (`CodeEditSpelling::setDocumentPath()`): it is not underlined there again
after the file is opened later, but still is in other files; without a file
it is ignored for the session only. A file manager moving, copying or deleting
files or directories calls `moveSpellingData()`, `copySpellingData()` or
`removeSpellingData()` so that the ignored words follow them.

The tables keep short rows once, in the order of their key (`WITHOUT ROWID`):
`words(language, word)`, `corrections(language, typed, correction)` and
`ignored(file, word)`; each file's path is stored once in `files(id, path)`,
sorted by its index. A directory's files are the range of paths from `dir/` up
to `dir0` (`0` follows `/`), found in the index at once: moving a directory
changes only those paths, and a file operation touching no file with ignored
words costs one lookup. The schema's version is `PRAGMA user_version`; a
database of 1.46 or 1.47 is converted when first opened.

`SpellChecker` can be used on its own (`check()`, `suggestions()`,
`addToDictionary()`, `ignore()`); it is not thread-safe.

## Suggestions

Checking a word is hunspell's. The suggestions come from all forms of the
dictionary: its stems with every affix rule applied (pl_PL: about 3.7 million
forms), kept in a cache file, `~/.cache/siplasplas/spell/<name>.forms`, built
in the background the first time (a few seconds), rebuilt when the dictionary
changes, and memory-mapped (shared by all programs, read on demand). The file
keeps no form as text: each is its stem and the rules applied to it (a prefix,
a suffix, a second suffix), 8 bytes, made again when read; the forms are
ordered by the hash of their class, with 16 bits of it in each, so a class
is found in its bucket at once (pl_PL: 43 MB, built with about 160 MB of
memory; pt_BR with 10.5 million forms: 124 MB). Until it
is ready, and for a dictionary with more than 30 million forms (Hungarian,
Galician, Arabic, Hebrew) or using
COMPLEXPREFIXES, hunspell's own suggestions are shown. Flags of every kind
(one character, long, numeric, UTF-8), flag aliases, NEEDAFFIX, CIRCUMFIX,
continuation classes (two suffixes; French elision, `l'aime`, as prefixes
allowed by a suffix) and forbidden words are known, so e.g. French, German,
Spanish, Portuguese, Catalan, Czech, Slovak, Latvian, Croatian, Serbian and
Turkish have their forms; compound words are hunspell's only. On a French text
typed without accents 53 of 60 mistakes get the right first suggestion
(hunspell alone: 47), 1.4 suggestions on average instead of 3.2, in 24 ms a
word instead of 91.

The forms are grouped by class: the letters of a word in lower case, without
diacritics (Polish, and the accents of other Latin letters: `é` → e, `ç` → c),
with the characters AltGr types on a letter key of the keyboard as the
letter of their key (Polish keyboard: `ð` → d, `©` → r, `ß` → t, `œ` → w), sorted.

The keyboard is one of the layouts in `spell/src/KeyboardLayouts.inc`, made
from the X keyboard layouts by `tools/gen_keyboard_layouts.py`: `pl`, `us`,
`de` (QWERTZ), `fr` (AZERTY), `es`, and `pl-windows` (Windows' Polish
programmers, where AltGr gives only Polish letters). The layout follows the
text's language and the system's keyboards (`keyboardLayoutFor()`): one of
the desktop's layouts for that language (KDE's or GNOME's setting, else
`/etc/default/keyboard`; on Windows the input language), else the desktop's
first, so an English text on a Polish keyboard is typed on `pl` whatever the
system's language; without the desktop's, the language's own layout (`pl`,
on Windows `pl-windows`), else `us`. A checker uses the layout of its first
dictionary's language; `setKeyboardLayout()` chooses one for every text.
It tells which keys neighbour and
which hand types them, and what AltGr held types: a character AltGr typed
on a letter key after a letter (© on r, ” on b of the Polish keyboard) is
AltGr not let go, and the word one misspelled word, when a part around it is
wrong and the word is right with it as its key's letter or without it, other
mistakes not counted (`SpellChecker::checkableWords()`): "któ©ych" →
których, "zró”" → zrób, "któ©re" → które, all sure. Otherwise it is the sign
itself and the parts are words of their own ("Firma©", "są”", „zrób”). The
forms file is
kept per layout (`<name>-<layout>.forms`). Swapped
letters, missing diacritics and a held AltGr keep the class; a letter too
many, too few or wrong is a neighbouring class. Only those classes are
ranked, by a weighted edit distance from the typed word: a missing diacritic
0.35 (a typed one removed 0.7, or 0.35 next to another diacritic, typed or
meant: AltGr held, pressed a letter early or a letter late, "żę" → "że",
"zą" → "żą"), two adjacent letters swapped 0.6 when the two hands type
them on the keyboard and 1.1 when one hand does ("serach" for "search": r
and a both left), a letter two places off 1.1, three places 1.6 (a swap right
after another adds 0.5, so "dziłaja" gives działają before działaj; a moved
letter also lacking its diacritic adds 0.2, "dzielci" → dzielić), a missing
letter 0.9 (1 for the first letter, seldom left out), a letter too many 1.2,
or 0.7 when its key touches both meant letters around it or repeats one
("bhyc": h between b and y) and 0.9 when it touches one, another letter 1.5
(1.1 on a key touching the meant one: "nonth" → month), for a capital inside the word that the
suggestion has not 0.35 right after the capitals the word begins with (Shift
held: "POłączyłem" → Połączyłem, which follows the word's capital; over three
letters still sure, over four not; AltGr held with it, on those letters or
on after a diacritic further in the word, costs nothing more: "ÞÓŁączyłęm"
scores as "POŁączyłem") and 1 after
a small letter (camelCase rather: "PołąCzyłem", "PoŁączyłem"), plus 1 for a
split into two words and
0.4 for a capital (a proper name) the typed word does not have. Hunspell's
suggestions are ranked the same way and break ties. The user's words are not
among the forms; kept by class in memory, they are looked up in the same
classes ("gemnini" → an added "gemini"), so even thousands of them cost
nothing. Typographic apostrophes count as straight ones and come back in the
suggestions ("d’echec" → d’échec; a word with straight ones gets straight
ones, also from hunspell's French OCONV). A hyphenated word with one wrong
part gets that part's suggestions in the whole ("six-nonth" → six-month).
Only suggestions close to
the best are shown (at most 0.6 worse, 0.3 with word frequencies), three at
most (or the requested number if smaller), and none scoring more than 3.4
(with the frequency penalty below), so a word far from every dictionary word
has no suggestion. `SpellChecker::scoredSuggestions()`
gives the scores (see below for the frequencies). `isConfident()` tells a single
suggestion at most 1.5 away
and scoring at most 1.04 with the frequency penalty,
without a capital the typed word lacks: sure enough to apply without asking
(correction while typing, "Sure corrections" in a review).

These costs and limits were fitted by `tools/spelltune` to 772 real typing
mistakes of a Polish text of 36,000 words, written on Windows' Polish
programmers' keyboard and corrected by hand: 60% missing diacritics, 8% a
missing letter (in the middle 48, at the end 10, at the start 1), 6% a letter
too many, 6% Shift or capitals, 5% another letter (a neighbouring key 23,
another 10), 3.5% two swapped letters (22 of 27 typed by the two hands; the
5 by one hand by different fingers). Tuned on half of the mistakes, checked on
the other half: one suggestion, the right one, for 335 of 382 (293 before),
and 261 sure corrections, none wrong (232 before). A second text, 407
mistakes in 23,000 words, never tuned on: 365 get one suggestion, the right
one (328 with the earlier values), none a wrong or no suggestion, all 407 the
right one first or among a few. The sure limits were then raised as far as
no sure correction of the two texts was wrong (1.5 and 1.04): 818 of 1171
mistakes are corrected surely. On a list of 100 real Polish typing mistakes
91 are first (hunspell alone: 71), in about 6 ms each.
A checker keeps the suggestions it found (per word) while its dictionaries,
user words, corrections or frequencies stay the same, so a second review of a
long text only looks for new words (6000 misspelled words: 45 s the first
time, under a second afterwards).
Hunspell's own suggestions take most of that time, so by default they are
asked only when the forms have no candidate: the same results on that list
(one tie broken differently elsewhere) in about 7 ms a word instead of 28;
`setHunspellSuggestions(Always)` adds them always. A dictionary whose words
compound freely (COMPOUNDFLAG or COMPOUNDBEGIN in its .aff: German, Dutch)
asks hunspell always too, since the forms have no compound words
("Betriebsystem" → Betriebssystem; about 20 ms a word in German). A word
with Shift held too long gets its one-capital spelling when the dictionary
knows it ("BEtriebssystem" → Betriebssystem, also a compound).

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
A suggestion's stem (hunspell's) adds 0.25 for each tenfold below 1000 per
million words; one not in the file counts as 0.3 per million (very rare); a
two-word suggestion takes its rarer word's: the penalty follows the logarithm
of the frequency, from 0 (1000 per million and more) to 0.88 (0.3 per million).
Only suggestions at most 0.3 worse
than the best are then shown. "dialac" gives działać (not dialach, dialag).
A suggestion spelled as close as a shown one is shown too when its penalty is
at most 0.8 above the best's, however far behind that puts it: "fiunding"
gets finding and funding, not a sure finding, while "bhyc" gets być alone
(hyc, not in the frequency table, scores 0.88 more).
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
context, sorted as in the text, alphabetically or by score, and how long
finding the corrections took. It is made for reviewing a text a kind of word
at a time, opened again for the next kind: "Show" lists the words of one kind
(`CheckedAtFirst`, a name of earlier versions; the application keeps the
user's choice): all words, sure corrections, a single suggestion, a single one
not sure, several suggestions with the first sure, with the first not sure,
or any, words without suggestions. Nothing is checked at first; an action applied checks or
unchecks all the words shown, or the rows selected (those two actions let
several rows be selected, with Ctrl and Shift). Correct corrects the checked
words of the kind shown only, as one undo step, takes them out of the list
and leaves the window open (the words left on those lines move with the
text); Correct and Close also closes it; the others stay for the next
review: e.g. sure corrections first (Check all, Correct), then single
suggestions not sure, looked over one by one. Ctrl+C or the context menu copies the
selected rows, separated by tabs, or their lines of text; a double click on
a row selects its word in the text and scrolls there, to read more of its
context while the window stays open. "Add to
Dictionary" and "Ignore" take the selected word out of the list, every row of
it (an English word or a coined one written on purpose); "Remember Correction"
keeps the selected word's chosen correction as the user's own
(`SpellChecker::addCorrection()`, in the database above, shared by programs):
from then on it is the word's only suggestion, score 0, so
sure, also while typing; "Own Correction" (Ctrl+F6) asks for one of the
user's own (starting from the word as typed) and keeps it the same way, also
for a word without suggestions, or replaces the word with the words before and
after it here only. The words of an own correction that neither the dictionaries nor
the user's words know (a name, a term) are ignored in the document, so in its
file when it has one. "User Dictionary" (`UserDictionaryDialog`) shows the
user's words and corrections and removes them.

`setCorrectWhileTyping(true)` (off by default) corrects a misspelled word when
a space or punctuation is typed right after it, if its single suggestion is
confident (see above) and no moved space is offered instead. Pasted text is not corrected
(only typed keys count), nor a word right in another installed language. The
correction is its own undo step, so undo brings back the word as typed;
`corrected()` reports it, e.g. for a status bar.
