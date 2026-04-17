# Syntax Highlighting — Design Document

Cel: dodać kolorowanie składni do `qcodeedit` z zachowaniem ścisłej separacji:

- **Komponent edycji (core `qcodeedit`)** nie czyta żadnych plików konfiguracyjnych i nie zna formatu XML. Dostaje *gotowe dane* przez API.
- **Demo (lub aplikacja użytkownika)** czyta pliki Kate `*.xml` z katalogu `~/.local/share/org.kde.syntax-highlighting/syntax/` i buduje instancje klas danych wyeksportowanych przez core.

Taka separacja pozwala:

- Używać core bez zależności od XML-a / KDE / qcodeedit można też sparować z innym źródłem reguł (JSON, kod).
- Przetestować engine bez dysku (testy jednostkowe tworzą `RulesHighlighter` programistycznie).

---

## 1. Warstwy

```
┌──────────────────────────────────────────────────────────────┐
│  WARSTWA 3 — DEMO                                            │
│                                                              │
│  KateXmlReader:                                              │
│    parsuje c.xml, cpp.xml, python.xml, ... →                 │
│    buduje RulesHighlighter (warstwa 2)                       │
│                                                              │
│  (w demo/ lub osobnym module, NIE w libqcodeedit)            │
└──────────────────────────────────────────────────────────────┘
                            │
                            │ setHighlighter(IHighlighter*)
                            ▼
┌──────────────────────────────────────────────────────────────┐
│  WARSTWA 2 — RULES ENGINE (libqcodeedit)                     │
│                                                              │
│  RulesHighlighter : public IHighlighter                      │
│    - tablica Context (każdy = lista Rule)                    │
│    - tablica KeywordList                                     │
│    - tablica TextAttribute (paleta)                          │
│    - highlightLine(line, stateIn) → (spans, stateOut)        │
│                                                              │
│  Nie ma pojęcia XML. Zna tylko swoje struktury danych.       │
│  Można go zbudować kodem, z JSON-a, z XML-a — dowolnie.      │
└──────────────────────────────────────────────────────────────┘
                            │
                            │ IHighlighter* (polimorfizm)
                            ▼
┌──────────────────────────────────────────────────────────────┐
│  WARSTWA 1 — CORE EDITOR (libqcodeedit)                      │
│                                                              │
│  CodeEditArea:                                               │
│    - trzyma IHighlighter* (nullable)                         │
│    - cache: QVector<int> m_lineEndStates                     │
│    - po każdej zmianie linii N: re-highlight od N aż stan    │
│      się ustabilizuje                                        │
│    - LineRenderer rysuje linię z użyciem spans               │
│                                                              │
│  Nie wie nic o regułach, regexach, XML. Wywołuje tylko       │
│  highlightLine() na IHighlighter.                            │
└──────────────────────────────────────────────────────────────┘
```

---

## 2. Model danych (publiczne API core)

### 2.1 `TextAttribute` — styl rysowania

```cpp
namespace qce {

struct TextAttribute {
    QColor foreground;        ///< główny kolor tekstu
    QColor background;        ///< invalid = brak tła (transparent)
    bool   bold      = false;
    bool   italic    = false;
    bool   underline = false;
    // (v2: strikeout, wavy-underline dla spell-checking)
};

} // namespace qce
```

Paleta atrybutów to `QVector<TextAttribute>`. Atrybut identyfikowany po indeksie (`int attributeId`), bo to szybkie i serializowalne.

Standardowe pozycje palety (konwencja, nie wymuszana przez core):
`Normal, Keyword, DataType, Decimal, Float, Char, String, Comment, Preprocessor, Symbol, Error, …`

Dokładnie jak Kate w `<itemDatas><itemData defStyleNum="dsXxx"/></itemDatas>`.

### 2.2 `StyleSpan` — zakres znaków z atrybutem

```cpp
struct StyleSpan {
    int start;         ///< indeks QChar w linii (kolumna logiczna, nie wizualna)
    int length;        ///< w znakach; 0 dopuszczalne (pusty span pomijany)
    int attributeId;   ///< indeks do palety; -1 = domyślny kolor tekstu
};
```

Spans są posortowane rosnąco po `start` i nie nakładają się. Fragmenty niepokryte = `-1` (domyślny foreground palety palety użytkownika, zwykle `palette().text()`).

### 2.3 `HighlightState` — stan międzyliniowy

Kate highlighter pracuje jako automat: koniec linii ma pewien stan (np. „jesteśmy wewnątrz komentarza `/* … */`"), który wpływa na kolorowanie następnej linii.

Reprezentacja: **stos kontekstów** (bo Kate obsługuje `#pop#pop`):

```cpp
struct HighlightState {
    QVector<int> contextStack;   ///< top = aktywny kontekst
    // może też zawierać dynamiczne "captured" stringi dla back-refs (v2)

    bool operator==(const HighlightState& o) const noexcept
    { return contextStack == o.contextStack; }
};
```

**Ważne**: core nie interpretuje zawartości `HighlightState`. Traktuje ją jako nieprzezroczysty token porównywalny `==`.

### 2.4 `IHighlighter` — interfejs używany przez edytor

```cpp
class IHighlighter {
public:
    virtual ~IHighlighter() = default;

    /// Stan na początku dokumentu (zwykle pojedynczy kontekst root, id=0).
    virtual HighlightState initialState() const = 0;

    /// Pokoloruj jedną linię.
    ///   line     — surowy tekst linii (bez '\n')
    ///   stateIn  — stan na końcu linii poprzedniej (lub initialState() dla linii 0)
    ///   spans    — WYJ: pokrycie linii atrybutami (posortowane, niepokrywające się)
    ///   stateOut — WYJ: stan na końcu tej linii; używany dla następnej
    virtual void highlightLine(const QString&       line,
                               const HighlightState& stateIn,
                               QVector<StyleSpan>&   spans,
                               HighlightState&       stateOut) const = 0;

    /// Paleta atrybutów. Rozmiar stały po skonfigurowaniu highlightera.
    virtual const QVector<TextAttribute>& attributes() const = 0;
};
```

Core trzyma `IHighlighter*` (non-owning); czas życia zarządza użytkownik.

---

## 3. `RulesHighlighter` — engine oparty na regułach

Konkretna implementacja `IHighlighter`, odpowiadająca 1-do-1 modelowi Kate.

### 3.1 Kontekst

```cpp
struct HighlightContext {
    QString name;
    int  defaultAttribute  = -1;   ///< attrib dla niepokrytego tekstu
    int  lineEndNextContext = -1;  ///< -1 = #stay; inaczej kontekst do przełączenia
    int  lineEndPopCount    = 0;   ///< ile #pop przed ewentualnym push
    bool fallthrough        = false;
    int  fallthroughContext = -1;
    QVector<HighlightRule> rules;  ///< próbowane w kolejności
};
```

### 3.2 Regularne reguły (`HighlightRule`)

```cpp
struct HighlightRule {
    enum Kind {
        DetectChar,       // pojedynczy QChar
        Detect2Chars,     // dokładnie 2 QChar
        AnyChar,          // pojedynczy QChar ∈ zbioru
        StringDetect,     // dokładny QString (case sens/insens)
        WordDetect,       // StringDetect + word-boundary
        RegExpr,          // QRegularExpression
        Keyword,          // słowo ∈ liście (KeywordList)
        DetectSpaces,     // jeden lub więcej whitespace
        DetectIdentifier, // [a-zA-Z_][a-zA-Z0-9_]*
        Int,              // integer literal
        Float,            // floating literal
        HlCChar,          // 'c' z C-escapes
        HlCStringChar,    // \n, \t, \x41 itp.
        HlCHex, HlCOct,   // 0x..., 0...
        LineContinue,     // \ na końcu linii
        RangeDetect,      // od char do char (single-line)
        IncludeRules      // "include" reguł z innego kontekstu
    };
    Kind kind;

    // Parametry specyficzne dla kind (tylko właściwe dla kind są ważne):
    QChar    ch, ch1;
    QString  str;
    bool     caseSensitive = true;
    QRegularExpression regex;
    int      keywordListId = -1;    // dla Kind::Keyword
    int      includedContextId = -1; // dla Kind::IncludeRules

    // Wspólne dla wszystkich reguł:
    int  attributeId    = -1;   ///< -1 = użyj context.defaultAttribute
    int  nextContextId  = -1;   ///< -1 = #stay; inaczej kontekst do pushu
    int  popCount       = 0;    ///< 0,1,2,... dla #pop / #pop#pop
    bool lookAhead      = false;///< nie konsumuj dopasowania
    bool firstNonSpace  = false;///< reguła tylko na pierwszym niespacji
    bool dynamic        = false;///< regex z %1 back-refs
};
```

### 3.3 Listy słów kluczowych

```cpp
struct KeywordList {
    QString            name;     ///< odwołanie z HighlightRule{Kind=Keyword}
    QSet<QString>      words;
    bool               caseSensitive = true;
    QString            additionalDeliminators; ///< dodatkowe znaki dzielące
    QString            weakDeliminators;       ///< znaki nie-dzielące
};
```

### 3.4 API konstrukcyjne

```cpp
class RulesHighlighter : public IHighlighter {
public:
    // Builder — tylko warstwa 3 (Kate XML reader / demo) używa tego API.
    int addContext(HighlightContext ctx);      ///< zwraca id
    int addKeywordList(KeywordList kw);        ///< zwraca id
    int addAttribute(TextAttribute attr);      ///< zwraca id
    void setInitialContextId(int id);
    void setAttributes(QVector<TextAttribute> palette);

    // IHighlighter:
    HighlightState initialState() const override;
    void highlightLine(...) const override;
    const QVector<TextAttribute>& attributes() const override;

private:
    QVector<HighlightContext> m_contexts;
    QVector<KeywordList>      m_keywords;
    QVector<TextAttribute>    m_attributes;
    int                       m_initialContextId = 0;
};
```

### 3.5 Jak engine obsługuje tekst w cudzysłowach i wielowierszowe komentarze

To są **dwa klasyczne przykłady** dla których model contexts + rules jest zaprojektowany:

#### Tekst w cudzysłowach (single-line)

```
Kontekst "Normal":
  DetectChar { ch='"',  attribute=String, nextContext=InString }

Kontekst "InString":
  defaultAttribute = String         ← nienadpisane znaki też są String
  lineEndNextContext = -1           ← #stay ALBO pop (zależy od języka)
  HlCStringChar { attribute=StringChar }   ← \n, \t, \x41
  DetectChar    { ch='"', attribute=String, popCount=1 }
```

#### Wielowierszowy komentarz `/* … */`

```
Kontekst "Normal":
  Detect2Chars { ch='/', ch1='*', attribute=Comment, nextContext=BlockComment }

Kontekst "BlockComment":
  defaultAttribute   = Comment
  lineEndNextContext = -1      ← #stay — zostajemy w BlockComment na kolejnej linii
  Detect2Chars { ch='*', ch1='/', attribute=Comment, popCount=1 }
```

**Punkt kluczowy**: stan „jesteśmy w komentarzu" przechodzi między liniami przez `HighlightState.contextStack`. Następna linia dostaje `stateIn` z `BlockComment` na szczycie stosu i engine startuje dopasowywanie reguł od `BlockComment`, a nie od `Normal`.

### 3.6 Algorytm `highlightLine`

```
pos = 0
stateOut = stateIn (kopia)
aktywny = ctx[top of stateOut.contextStack]

while pos < line.size():
    // znajdź pierwszą regułę kontekstu która dopasuje się od pos
    firstMatch = null
    firstLen = 0
    for rule in aktywny.rules:
        if rule.firstNonSpace and pos != first_non_ws_pos(line): skip
        len = matchAt(rule, line, pos)   // 0 = brak dopasowania
        if len > 0:
            firstMatch = rule
            firstLen   = len
            break

    if firstMatch:
        attr = firstMatch.attributeId ?: aktywny.defaultAttribute
        if not firstMatch.lookAhead:
            emit span(pos, firstLen, attr)
            pos += firstLen
        // context switch:
        for i in 0..firstMatch.popCount: stateOut.contextStack.pop()
        if firstMatch.nextContextId >= 0:
            stateOut.contextStack.push(firstMatch.nextContextId)
        aktywny = ctx[top]
    else:
        // nic nie dopasowało: znak bez reguły → default attribute kontekstu
        emit span(pos, 1, aktywny.defaultAttribute)
        pos += 1

// koniec linii — obsłuż lineEndContext
for i in 0..aktywny.lineEndPopCount: stateOut.contextStack.pop()
if aktywny.lineEndNextContext >= 0:
    stateOut.contextStack.push(aktywny.lineEndNextContext)
```

Emit span: scala z poprzednim jeśli ten sam `attributeId` i styka się (`start+length == new.start`).

---

## 4. Inkrementalny highlight w edytorze

Edytor trzyma:

```cpp
QVector<HighlightState> m_lineEndStates; // rozmiar = lineCount
// m_lineEndStates[i] = stan na końcu linii i (po przetworzeniu)
```

### 4.1 Po edycji linii `n`

```
stateIn = (n == 0) ? highlighter->initialState() : m_lineEndStates[n-1]
for i = n; i < lineCount; ++i:
    highlightLine(lineAt(i), stateIn, spans, stateOut)
    if i < m_lineEndStates.size() and stateOut == m_lineEndStates[i]:
        break       // od tej linii stan już się nie zmienia — stop
    m_lineEndStates[i] = stateOut
    stateIn = stateOut
    invalidate_visual(i)   // zaznacz do repaint
```

### 4.2 Insert/remove linii

Rozmiar `m_lineEndStates` zmienia się o odpowiednio +/-N; od pierwszej dotkniętej linii przeprowadzamy algorytm z 4.1.

### 4.3 Lazy highlight linii widocznych

Dla dużych plików: inicjalnie `m_lineEndStates[i]` = "unknown". `LineRenderer` w `paint()` pyta edytor o `spansForLine(i)`; ten on-demand woła highlighter od ostatnio znanego stanu aż do `i`. Raz policzone stany są zapamiętane.

---

## 5. Rysowanie

### 5.1 API `LineRenderer`

```cpp
void LineRenderer::setAttributePalette(const QVector<TextAttribute>&);
// per-line hook — jeżeli nie ustawiony, rysuje jednokolorowo:
using SpansForLineFn = std::function<const QVector<StyleSpan>*(int line)>;
void LineRenderer::setSpansProvider(SpansForLineFn);
```

### 5.2 Renderowanie linii ze spans

Dla każdej widocznej linii:

1. Pobierz `spans = provider(lineIndex)` (lub `nullptr` → rysuj bez stylowania).
2. Iteruj znaki linii, trzymając cursor w `spans` (binary search na `start` lub pointer advance). Dla każdego znaku:
   - wybierz atrybut (span aktywny lub `-1` → `palette().text()`),
   - jeśli atrybut taki sam jak w poprzednim znaku: buffuj,
   - inaczej: rysuj zbuffowany fragment + zmień pen/font, zacznij nowy bufor.
3. Na końcu wyrzuć ostatni bufor.

Pen ustawiany z `TextAttribute.foreground`; `QFont` z bold/italic/underline jeśli atrybut je ma.

### 5.3 Współpraca z selekcją i whitespace markers

- **Selekcja** (invert mode): drugi pass nadal działa — rysuje białe znaki w kolorze z clip-region, niezależnie od kolorów składni.
- **Whitespace markers** (·, →): drugi pass z kolorem wyciszonym; ignoruje spans.
- **Caret** (kreska / blok): rysowany na końcu, niezależnie.

---

## 6. Czytnik Kate XML (WARSTWA 3, w demo)

### 6.1 Rola

Klasa `KateXmlReader` (nazwa przykładowa):

- otwiera `c.xml`,
- parsuje DTD-entities (`<!ENTITY int "…">` — używane w regexach),
- buduje `RulesHighlighter` przez jego builder API,
- zwraca gotowy `std::unique_ptr<IHighlighter>` do podpięcia w `CodeEditArea::setHighlighter()`.

**Nie istnieje w libqcodeedit**. Żyje w `demo/KateXmlReader.cpp` (lub osobnym module `qcodeedit-kate/`).

### 6.2 Mapowanie XML → struktury

| Kate XML | → | core |
|---|---|---|
| `<itemDatas><itemData defStyleNum="dsKeyword" .../></itemDatas>` | → | `addAttribute()` — kolor z bieżącego motywu (np. Solarized) |
| `<list name="controlflow"><item>if</item>…</list>` | → | `addKeywordList()` |
| `<contexts><context name="Normal" …><…/></context></contexts>` | → | `addContext()` + każda pod-reguła → `HighlightRule` |
| `<DetectChar char="{" attribute="Symbol" context="#stay"/>` | → | `Rule{Kind=DetectChar, ch='{', attributeId=…, nextContextId=-1, popCount=0}` |
| `<Detect2Chars char="/" char1="*" context="Comment"/>` | → | `Rule{Kind=Detect2Chars, ch='/', ch1='*', nextContextId=id("Comment")}` |
| `<RegExpr String="\.?[0-9]" context="Number" lookAhead="1"/>` | → | `Rule{Kind=RegExpr, regex, lookAhead=true}` |
| `<keyword String="controlflow" attribute="Control Flow"/>` | → | `Rule{Kind=Keyword, keywordListId=id("controlflow")}` |
| `<IncludeRules context="match keywords"/>` | → | `Rule{Kind=IncludeRules, includedContextId=id("match keywords")}` |
| `context="#stay"` | → | `nextContextId=-1, popCount=0` |
| `context="#pop"` | → | `popCount=1` |
| `context="#pop#pop"` | → | `popCount=2` |
| `context="#pop#pop!Name"` | → | `popCount=2, nextContextId=id("Name")` |
| `context="CommentStart"` | → | `nextContextId=id("CommentStart")` |

### 6.3 DTD entities

```xml
<!ENTITY int "(?:[0-9](?:'?[0-9]++)*+)">
```

`QXmlStreamReader` sam rozwija `&int;` w atrybutach (Qt ustawia `DtdHandling` na `IncludeExternalGeneralEntities`? — trzeba zweryfikować; w najgorszym wypadku samemu podmienić na podstawie `<!ENTITY …>`).

### 6.4 Rozwiązywanie odwołań *po nazwach*

Kate XML używa nazw (`context="Comment"`, `attribute="String"`, `String="controlflow"` dla `<keyword>`). Reader:

1. **Pass 1** — stwórz puste konteksty i atrybuty, zapamiętaj `QHash<QString,int>` dla każdej kategorii.
2. **Pass 2** — dopełnij reguły rozwiązując id-y.

To rozwiązuje forward-references (context `Normal` odwołuje się do `Number`, który występuje niżej).

### 6.5 Pomijanie regionów składania (folding)

Atrybuty `beginRegion`/`endRegion` w v1 są **ignorowane**. Model danych nie ma słowa o foldingu — to niezależna feature.

---

## 7. API użytkownika (końcowe)

### 7.1 Publiczne nagłówki

```
include/qce/
    TextAttribute.h    (struct TextAttribute)
    StyleSpan.h        (struct StyleSpan)
    HighlightState.h   (struct HighlightState)
    IHighlighter.h     (class IHighlighter)
    RulesHighlighter.h (class RulesHighlighter + HighlightRule, HighlightContext, KeywordList)
```

### 7.2 Użycie w demo

```cpp
#include <qce/RulesHighlighter.h>
#include "KateXmlReader.h"  // tylko w demo

auto hl = KateXmlReader::load(
    "/home/andrzej/.local/share/org.kde.syntax-highlighting/syntax/c.xml",
    kateTheme);  // theme → kolory

editor->area()->setHighlighter(hl.get());
// ...edytor używa hl via IHighlighter, nie wie o Kate/XML
```

### 7.3 Test — highlighter programistyczny (bez XML)

```cpp
auto hl = std::make_unique<RulesHighlighter>();
int attrNormal  = hl->addAttribute({QColor("black")});
int attrString  = hl->addAttribute({QColor("#d14")});
int attrComment = hl->addAttribute({QColor("#998"), {}, false, true});  // italic

int ctxNormal   = hl->addContext({"Normal", attrNormal, -1, 0});
int ctxString   = hl->addContext({"String", attrString, -1, 0});
int ctxComment  = hl->addContext({"Comment", attrComment, -1, 0});

hl->contextRef(ctxNormal).rules = {
    {Rule::DetectChar, '"', {}, {}, {}, false, {}, -1, -1, attrString, ctxString},
    {Rule::Detect2Chars, '/', '*', {}, {}, false, {}, -1, -1, attrComment, ctxComment},
};
hl->contextRef(ctxString).rules = {
    {Rule::DetectChar, '"', {}, {}, {}, false, {}, -1, -1, attrString, -1, 1}, // #pop
};
hl->contextRef(ctxComment).rules = {
    {Rule::Detect2Chars, '*', '/', {}, {}, false, {}, -1, -1, attrComment, -1, 1},
};
```

Taki highlighter można jednostkowo przetestować bez żadnego pliku.

---

## 8. Etapy implementacji

| Etap | Zakres | Testy | Kolor w edytorze? |
|---|---|---|---|
| **A** | Model danych: `TextAttribute`, `StyleSpan`, `HighlightState`, `IHighlighter` (tylko nagłówki) | — | nie |
| **B** | `RulesHighlighter` — kontekst + podstawowe reguły: `DetectChar`, `Detect2Chars`, `StringDetect`, `Keyword`, `DetectSpaces`, `DetectIdentifier` | testy jednostkowe z programowanym highlighterem (keyword, string, comment) | nie |
| **C** | Integracja z `CodeEditArea`: `m_lineEndStates`, inkrementalne highlightowanie, spans→`LineRenderer` | test re-highlight rozszerzonego komentarza / niezamkniętego stringa | **tak (pierwsze kolory)** |
| **D** | Dodanie `RegExpr`, `WordDetect`, `AnyChar`, `Int`, `Float`, `HlCStringChar`, `LineContinue`, `IncludeRules`, `RangeDetect` | testy jednostkowe per reguła | pełne pokrycie v1 |
| **E** | `KateXmlReader` w demo; parsing `c.xml`, theme do kolorów | test integracyjny: `c.xml` → highlight pliku `.c` | demo z podświetlaniem |
| **F** | Dynamic regex backrefs, folding regions, spell-checking — jeśli potrzebne. Poza v1. | — | — |

### Sugerowana kolejność w repo

- Etap A + B + połowa C → tag `v0.7.0` (szkielet syntaksu — programowany tylko)
- Etap D + E → tag `v0.8.0` (Kate XML, pokolorowany C/C++ w demo)

---

## 9. Przyszłe rozszerzenia (poza v1)

- **Folding regions** — `beginRegion`/`endRegion` → zwijanie bloków.
- **Dynamic rules** — backrefs dla heredocs (`<<EOF` … `EOF`) i wstrzykniętego tekstu.
- **Podświetlanie nawiasów pod kursorem** — niezależny mechanizm (nie syntaksy, tylko cursor-aware decorator).
- **Spell-checking** — `spellChecking="true"` na `itemData` → drugi pass z falistym podkreśleniem źle pisanych słów.
- **Wyszukiwanie** — podświetlanie wyników wyszukiwania jako warstwa nad syntaksą.
- **Inne formaty** — własny JSON-owy dialekt, TextMate grammars (`.tmLanguage`), Tree-sitter.
