# Grammar (qcodeedit-grammar)

Grammar checking of nearby words by their readings, for real words in the
wrong place that spelling cannot see: "ze" typed for "że", "piękna
dziewczynę" for "piękną dziewczynę", "Mam dzwoniła" for "Mama dzwoniła".
Plain C++20, no Qt and no other library, so that tools without a GUI can use
it; `qcodeedit-spell` shows its findings in the editor through the
`GrammarChecker` interface (see [SPELL.md](SPELL.md)).

## The morphology file

`qce::grammar::Morphology` reads a morphology file: every reading (lemma,
tag, label, qualifiers) of every form of a morphological dictionary, as a
minimal acyclic automaton in which forms share their beginnings and readings
their ends. For Polish, made from PoliMorf (BSD licence), it takes about
5 MB and is read in a few milliseconds.

```cpp
#include <qce/grammar/Morphology.h>

qce::grammar::Morphology morphology;
std::string error;
if (morphology.load(path, &error))
    for (const auto& reading : morphology.analyse("mam"))
        ;  // mieć fin:sg:pri:imperf, mama subst:pl:gen:f, mamić impt:sg:sec:imperf
```

`MorphologyBuilder` makes the file of readings added one by one (`add()`,
then `write()`); the spellmorf tools read PoliMorf and use it. Tags follow
the NKJP/SGJP tagset (`qce/grammar/Tag.h`).

## The rules

```cpp
#include <qce/grammar/Grammar.h>

qce::grammar::GrammarRules rules;
qce::grammar::loadGrammarRules("pl.grammar", rules, &error);
qce::grammar::Grammar grammar(morphology);
grammar.setRules(rules);
for (const auto& finding : grammar.check(qce::grammar::tokenize(text)))
    ;  // finding.token, finding.rule, finding.suggestions (token, word)
```

- **government**: a preposition before a word in no case it takes; the forms
  with "e" ("ze", "we") stand before a noun phrase only, so "ze jest" offers
  "że";
- **agreement**: an adjective right before a noun not agreeing with it in
  number, case and gender (complements and attributes of a noun before
  allowed);
- **verbs**: two finite verbs next to each other when a word one typing slip
  away can be the second one's subject ("Mam dzwoniła": "Mama");
- **rare word**: a word known only as rare, one diacritic from a common one.

For a broken rule, words one typing slip away that fit are offered (a
diacritic; two letters swapped or a neighbouring key, the first letter kept;
a letter missing or too many after the first).

The words of a language the rules need (idioms such as "za mąż", pronouns,
conjunctions, the reflexive pronoun, qualifiers of rare and archaic
readings, letters, diacritics, keyboard rows) are in its rules file,
`grammar/data/pl.grammar`, installed in `share/qcodeedit/grammar`; another
language is another file.

Two optional tables, made by the spellmorf tools from corpora, improve the
rules: `loadForms()` (how often each form is each part of speech: "mam" is
nearly always "mieć") and `loadComplements()` (the cases adjectives take as
complements: "wdzięczny ojcu").
