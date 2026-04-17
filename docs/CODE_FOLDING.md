# Code Folding — Design Document

Cel: dodać zwijanie fragmentów kodu (bloki `{}`, komentarze wieloliniowe, regiony `#ifdef/#endif`, użytkowe markery `//BEGIN/END`) do `qcodeedit`.

Konsekwentnie z zasadą z [SYNTAX_HIGHLIGHTING.md](SYNTAX_HIGHLIGHTING.md): **core nie wie o konkretnym języku ani o formacie pliku opisującego reguły**. Dane otrzymuje przez API. Demo (lub aplikacja użytkownika) dostarcza implementacje `IFoldingProvider`.

---

## 1. Dwa źródła informacji o fold-ach

### 1.1 Kate XML (`beginRegion` / `endRegion` na regułach highlighter-a)

Pliki `c.xml`, `cpp.xml`, `dot.xml` itp. oznaczają początek/koniec regionu **tymi samymi regułami które podświetlają składnię**:

```xml
<!-- c.xml -->
<DetectChar char="{"    beginRegion="Brace1" />
<DetectChar char="}"    endRegion="Brace1"   />
<Detect2Chars char="/" char1="*"  beginRegion="Comment" />
<Detect2Chars char="*" char1="/"  endRegion="Comment"   />
<WordDetect String="ifdef"   beginRegion="PP"  lookAhead="true" />
<WordDetect String="endif"   endRegion="PP"                     />
<WordDetect String="elif"    endRegion="PP" beginRegion="PP" />   <!-- zamyka + otwiera -->
<StringDetect String="//BEGIN" beginRegion="Region1" firstNonSpace="true" />
<StringDetect String="//END"   endRegion="Region1"   firstNonSpace="true" />

<!-- dot.xml -->
<DetectChar char="{"  beginRegion="curly"  context="RegionCurly" />
<DetectChar char="["  beginRegion="square" context="RegionSquare" />
<DetectChar char="}"  endRegion="curly"  context="#pop" />
<Detect2Chars char="/" char1="*" beginRegion="Comment" context="CommentML" />
```

**Obserwacje**:

- Region jest *nazwany* (`Brace1`, `curly`, `Comment`, `PP`). Nazwy służą do parowania: `endRegion="curly"` może domknąć tylko otwarcie o tej samej nazwie.
- Regiony mogą się zagnieżdżać — `{ … { … } … }` to dwa zagnieżdżone `curly`.
- Reguła może mieć **jednocześnie** `endRegion` i `beginRegion` (np. `elif` — kończy poprzedni blok `#if` i otwiera nowy).
- Początek regionu jest *na* znaku `{`, koniec jest *za* znakiem `}`. Placeholder zastępuje tekst między nimi.

### 1.2 Semantyczny builder (JetBrains-style)

Plugin `dot-plus` (`DotFoldingBuilder.java`) obrazuje bogatszy model niż Kate XML:

```java
// foldStmtBlock — zwiń blok {...} w DOT
var rightBrace = stmtBlock.getLastChild();
var start = stmtBlock.getFirstChild().getTextRange().getStartOffset();
var end   = rightBrace.getTextRange().getEndOffset();
result.add(new FoldingDescriptor(node, TextRange.create(start, end),
                                 null, "{...}"));

// foldBlock — grupuj KILKA kolejnych komentarzy jako JEDEN region
if (PsiTreeUtil.prevLeaf(element) instanceof PsiComment) return; // tylko pierwszy
var next = element;
while (PsiTreeUtil.nextLeaf(next) instanceof PsiComment) next = ...;
result.add(new FoldingDescriptor(node, range, null,
                                 firstLine + " ...", collapsedByDefault, …));
```

**Kluczowe zdolności których Kate nie da za darmo**:

- Grupowanie kilku elementów w jeden region (np. ciąg `//…\n//…\n//…` → jedna zwijka).
- `collapsedByDefault` zależny od *kontekstu* (header pliku vs zwykły blok).
- Placeholder zależny od treści (pierwsza linia komentarza + `...`).
- Folding niezależny od highlightera — operuje na AST / tokenach / tekście.

### 1.3 Wniosek — warstwa core musi obsługiwać oba modele

Core oferuje **generyczny interfejs** `IFoldingProvider`. Dwie dostarczane implementacje:

1. **`RuleBasedFoldingProvider`** — czyta markery `begin/endRegion` ze stanu silnika `RulesHighlighter`. Darmowy dla każdego języka z Kate XML.
2. **`SemanticFoldingProvider`** (abstrakcyjny helper) — użytkownik dziedziczy i implementuje logikę bloków (jak `DotFoldingBuilder`). Ma pełny dostęp do tekstu dokumentu, ale nie do AST (core nie ma parsera).

Użytkownik końcowy może łączyć kilka providerów (`CompositeFoldingProvider`): rule-based + własny grupowanie-komentarzy.

---

## 2. Architektura

```
┌────────────────────────────────────────────────────────────┐
│  WARSTWA 3 — DEMO / Aplikacja                              │
│                                                            │
│  KateXmlReader parsuje c.xml → buduje RulesHighlighter     │
│                          i RuleBasedFoldingProvider        │
│                                                            │
│  (Opcjonalnie) CustomFoldingProvider pisany ręcznie        │
│                — np. grupa komentarzy w DOT                │
│                — np. folding po indentacji (Python)        │
└────────────────────────────────────────────────────────────┘
                          │
                          │ setFoldingProvider(IFoldingProvider*)
                          ▼
┌────────────────────────────────────────────────────────────┐
│  WARSTWA 2 — Core Folding (libqcodeedit)                   │
│                                                            │
│  IFoldingProvider       → computeRegions(doc)              │
│  RuleBasedFoldingProvider (używa IHighlighter)             │
│  CompositeFoldingProvider (wrapper)                        │
│                                                            │
│  FoldState              — zbiór zwiniętych regionów        │
│  LineVisibilityMap      — mapowanie logical→visual z       │
│                           uwzględnieniem zwinięć           │
└────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────────┐
│  WARSTWA 1 — CodeEditArea                                  │
│                                                            │
│  - trzyma IFoldingProvider*, FoldState                     │
│  - integruje LineVisibilityMap z WrapLayout                │
│  - renderuje placeholder na pierwszej linii zwinięcia      │
│  - FoldingGutter (nowy IMargin): rysuje [+] [-] i pionowe  │
│    linie grupujące; klik → toggle                          │
│  - klawisze: Ctrl+- (fold), Ctrl+= (unfold), Ctrl+Shift+-  │
│    (fold all), Ctrl+Shift+= (unfold all)                   │
└────────────────────────────────────────────────────────────┘
```

---

## 3. Model danych (publiczne API core)

### 3.1 `FoldRegion`

```cpp
namespace qce {

struct FoldRegion {
    int     startLine;      ///< linia początku (0-based)
    int     startColumn;    ///< kolumna początku marker-a (np. kolumna znaku '{')
    int     endLine;        ///< linia końca
    int     endColumn;      ///< kolumna ZA ostatnim znakiem marker-a (np. za '}')
    QString placeholder;    ///< tekst po zwinięciu; pusty = "…"
    bool    collapsedByDefault = false;
    QString group;          ///< nazwa (Kate: "curly", "Comment"); tylko dla debug/UX
    int     depth = 0;      ///< głębokość zagnieżdżenia (liczona przez core, nie providera)
};

} // namespace qce
```

**Konwencje**:

- Region jest *single-line* gdy `startLine == endLine` — takie regiony są **ignorowane** przez core (nie ma sensu zwijać fragmentu jednej linii; to zadanie inline-fold, poza v1).
- Placeholder jest czysto tekstowy. Core rysuje go w szarej ramce na końcu linii `startLine` (styl standardowy).
- `depth` wypełnia core po posortowaniu i sprawdzeniu zagnieżdżenia — provider tego nie musi wypełniać.

### 3.2 `IFoldingProvider`

```cpp
class IFoldingProvider {
public:
    virtual ~IFoldingProvider() = default;

    /// Zwraca WSZYSTKIE regiony dla bieżącego dokumentu.
    /// Wołane przez edytor po każdej istotnej zmianie (podobnie jak highlighter).
    /// Wynik nie musi być posortowany — core sam sortuje i liczy depth.
    virtual QVector<FoldRegion> computeRegions(const ITextDocument* doc) const = 0;
};
```

Semantyka:

- Prosty, *pełny przebieg* na całym dokumencie. Inkrementalność jest *opcjonalną optymalizacją* wewnątrz providera (cache).
- Provider jest *const* względem dokumentu — nie wolno mu modyfikować tekstu.

---

## 4. `RuleBasedFoldingProvider`

### 4.1 Integracja z highlighter-em

`HighlightRule` zostaje rozszerzone o markery foldingu:

```cpp
struct HighlightRule {
    // ... (poprzednie pola z SYNTAX_HIGHLIGHTING.md)
    int beginRegionId = -1;   ///< id regionu otwartego przez tę regułę (-1 = brak)
    int endRegionId   = -1;   ///< id regionu zamkniętego przez tę regułę (-1 = brak)
    // UWAGA: jedna reguła może mieć oba (przykład: Kate 'elif' endRegion="PP" beginRegion="PP")
};
```

Mapowanie `nazwa → id` trzyma `RulesHighlighter`. Nazwy nie są potrzebne w core — to szczegół XML-a.

**Highlighter podczas `highlightLine` emituje oprócz spans także zdarzenia fold-marker**. Żeby nie komplikować interfejsu `IHighlighter`, zdarzenia są dostępne z `RulesHighlighter` jako rozszerzenie konkretnej klasy:

```cpp
class RulesHighlighter : public IHighlighter {
public:
    struct FoldEvent {
        int line;
        int column;      ///< kolumna znaku marker-a w linii
        int tokenLength; ///< długość dopasowanego tokena (np. 2 dla "/*")
        int regionId;
        bool isBegin;    ///< true=begin, false=end
    };

    /// Wariant highlightLine który zwraca też zdarzenia foldingu.
    void highlightLineEx(const QString& line,
                          const HighlightState& stateIn,
                          QVector<StyleSpan>& spans,
                          HighlightState& stateOut,
                          QVector<FoldEvent>& foldEvents,
                          int lineIndex) const;
};
```

### 4.2 Algorytm `computeRegions`

```
provider:
    regions = []
    open = stack<(regionId, startLine, startCol)>
    state = highlighter.initialState()

    for li = 0..lineCount-1:
        line = doc->lineAt(li)
        events = []
        highlighter.highlightLineEx(line, state, spans, state, events, li)
        for ev in events:
            if ev.isBegin:
                open.push({ev.regionId, li, ev.column})
            else: // end
                // szukamy w stacku od góry pierwszego openu o tym samym regionId
                idx = open.findLastMatching(ev.regionId)
                if idx >= 0:
                    opened = open[idx]
                    // zamknij ten region
                    regions.append(FoldRegion{
                        startLine=opened.startLine,
                        startColumn=opened.startCol,
                        endLine=li,
                        endColumn=ev.column + ev.tokenLength,
                        group=name_of(ev.regionId),
                    })
                    // popuj wszystkie niedomknięte wyżej (unbalanced) — zachowanie Kate
                    open.truncateTo(idx)

    // Niezamknięte regiony na końcu dokumentu: zamykamy na ostatniej linii (opcjonalnie).
    // Kate je ignoruje — w v1 robimy tak samo.

    return regions
```

**Uwagi**:

- Kate zamyka region *"ostatnio otwartym tego imienia"* — to nie stos per-name; to pojedynczy stos ze szukaniem wzwyż po `regionId`. Niedomknięte regiony *pod* zamykanym są porzucane.
- Dla języków z dobrze sparowanymi nawiasami ({}, [], ()) to działa idealnie.
- Dla `elif` (Kate: `endRegion="PP" beginRegion="PP"`) kolejność zdarzeń w linii = kolejność reguł w XML-u. Nasze `highlightLineEx` emituje je w tej samej kolejności. Algorytm: najpierw end, potem begin → poprawnie zamyka `#if` i otwiera nowy blok dla `elif`.

### 4.3 Placeholder

Dla rule-based providera placeholder jest generowany automatycznie:

| Grupa Kate | Placeholder |
|---|---|
| `Brace1`, `curly`, `square`, `paren` | `"{…}"`, `"[…]"`, `"(…)"` (zależnie od group) |
| `Comment` | `/* … */` |
| `PP` | `"#if … #endif"` |
| `Region1` (user markers) | treść linii begin po marker-ze, albo `"…"` |

Mapowanie group→template trzyma `RuleBasedFoldingProvider` jako `QHash<QString,QString>` konfigurowalne przez builder API. Domyślne wartości są rozsądne; demo może nadpisać.

---

## 5. `SemanticFoldingProvider` — wzór JetBrains

Dla fold-ów które nie wyrażają się markerami begin/end w sposób naturalny (np. "grupa N komentarzy pod rząd", "blok indentowany w Python"):

```cpp
class SemanticFoldingProvider : public IFoldingProvider {
public:
    QVector<FoldRegion> computeRegions(const ITextDocument* doc) const override final {
        QVector<FoldRegion> out;
        visit(doc, out);
        return out;
    }
protected:
    /// Hook do zaimplementowania przez użytkownika. Iteruj linie / tokeny,
    /// dodawaj FoldRegion do `out`.
    virtual void visit(const ITextDocument* doc, QVector<FoldRegion>& out) const = 0;
};
```

### 5.1 Wzór z `DotFoldingBuilder` — grupowanie komentarzy

```cpp
class DotCommentGroupFolder : public SemanticFoldingProvider {
protected:
    void visit(const ITextDocument* doc, QVector<FoldRegion>& out) const override {
        const int n = doc->lineCount();
        int i = 0;
        while (i < n) {
            if (!isCommentLine(doc->lineAt(i))) { ++i; continue; }
            int j = i;
            while (j + 1 < n && isCommentLine(doc->lineAt(j + 1))) ++j;
            if (j > i) { // co najmniej 2 linie — warto zwijać
                FoldRegion r;
                r.startLine   = i;
                r.startColumn = 0;
                r.endLine     = j;
                r.endColumn   = doc->lineAt(j).size();
                r.placeholder = doc->lineAt(i).trimmed() + QStringLiteral(" …");
                r.collapsedByDefault = (i == 0); // header pliku — zwiń
                r.group = QStringLiteral("CommentGroup");
                out.append(r);
            }
            i = j + 1;
        }
    }
};
```

Używanie:

```cpp
auto composite = std::make_unique<CompositeFoldingProvider>();
composite->add(std::make_unique<RuleBasedFoldingProvider>(ruleHl.get()));
composite->add(std::make_unique<DotCommentGroupFolder>());
editor->area()->setFoldingProvider(composite.get());
```

### 5.2 `CompositeFoldingProvider`

```cpp
class CompositeFoldingProvider : public IFoldingProvider {
    QVector<std::unique_ptr<IFoldingProvider>> m_providers;
public:
    void add(std::unique_ptr<IFoldingProvider> p) { m_providers.push_back(std::move(p)); }
    QVector<FoldRegion> computeRegions(const ITextDocument* doc) const override {
        QVector<FoldRegion> all;
        for (auto& p : m_providers) all += p->computeRegions(doc);
        return all;
    }
};
```

Core po otrzymaniu wyniku **sortuje** (`startLine, startColumn`), **normalizuje zagnieżdżenia** (usuwa overlap-y które nie są proper-nested) i **liczy depth**. Jeśli dwa providery produkują ten sam region (duplikat) — dedup po (start, end).

---

## 6. Stan zwinięć i mapowanie widoczności

### 6.1 `FoldState`

```cpp
class FoldState {
public:
    void setRegions(QVector<FoldRegion> regions);          ///< z providera
    const QVector<FoldRegion>& regions() const;

    bool isCollapsed(int regionIndex) const;
    void setCollapsed(int regionIndex, bool c);
    void toggle(int regionIndex);

    /// Region który zaczyna się na `line`; -1 jeśli brak.
    int regionStartingAt(int line) const;

    /// Najbardziej zewnętrzny ZWINIĘTY region zawierający `line` (inclusive
    /// od startLine, ale ukrywa linie `startLine+1..endLine`). -1 jeśli
    /// żaden zwinięty region nie ukrywa tej linii.
    int collapsedRegionHidingLine(int line) const;

    /// Czy linia jest widoczna (nie ukryta przez żadne zwinięcie).
    bool isLineVisible(int line) const;

    void foldAll();
    void unfoldAll();
    /// Zwiń regiony na danej głębokości (np. foldToLevel(1) → tylko outermost).
    void foldToLevel(int level);

private:
    QVector<FoldRegion> m_regions;       // posortowane po (startLine, startColumn)
    QSet<int>           m_collapsed;     // indeksy do m_regions
};
```

Kluczowa operacja: `isLineVisible(line)`. Linie `startLine+1 .. endLine` są ukryte gdy region jest zwinięty. Linia `startLine` zawsze widoczna (to na niej rysujemy placeholder). Dla zagnieżdżonych — wystarczy że *jeden* outer jest zwinięty.

### 6.2 `LineVisibilityMap` i integracja z `WrapLayout`

Dotychczas mamy `WrapLayout` który mapuje logical → visual row-y. Folding dokłada warstwę:

```
logical lines → (filter: isLineVisible) → visible logical lines → (wrap) → visual rows
```

Dwie opcje implementacji:

**Opcja A (zalecana)**: `WrapLayout::rebuild()` dostaje `FoldState*` i pomija linie ukryte. `m_lineFirstRow` zawiera `-1` dla ukrytych linii (albo używamy oddzielnej tabeli `lineToFirstVisibleRow`).

**Opcja B**: osobna warstwa `LineVisibilityMap` między document a WrapLayout, która produkuje sekwencję widocznych logical-lines; WrapLayout działa na tej sekwencji.

Opcja A jest prostsza (mniej warstw) i już wymaga tylko modyfikacji istniejącej klasy.

### 6.3 Placeholder w renderingu

`ViewportState::RowInfo` rozszerzamy o:

```cpp
struct RowInfo {
    int  logicalLine;
    int  startCol, endCol;
    bool isFirstRow;

    // Nowe:
    int     foldPlaceholderRegion = -1;  ///< jeśli >=0: na tej row-ie po endCol rysujemy placeholder
};
```

`LineRenderer` po narysowaniu segmentu linii sprawdza `row.foldPlaceholderRegion`; jeśli niezero, rysuje placeholder z `FoldState::regions()[id].placeholder` w ramce:

```
  int foo() {...}     ← placeholder "{...}" rysowany po visual-end tekstu linii otwierającej
```

Ramka: cienka linia wokół tekstu + szare tło + kolor `QPalette::Text` z alpha 0.6. Wymiary: `fm.horizontalAdvance(placeholder) + 2*padding`.

### 6.4 Pozycja kursora przy edycji

- Kliknięcie w placeholder → rozwiń region, umieść kursor na początku rozwiniętego obszaru (startLine+1, col 0).
- Edycja wewnątrz zwiniętego regionu jest niemożliwa (niewidoczne linie). Jeśli skrypt/klawisz przenosi kursor na ukrytą linię → *rozwijamy* najbardziej zewnętrzny region ukrywający tę linię.
- Po rebuild foldingu (po edycji) przenosimy stary stan `m_collapsed` na nowe regiony heurystycznie: jeśli region o (startLine, startCol, group) istniał i był zwinięty → nowy o tych samych cechach też jest zwinięty.

---

## 7. `FoldingGutter` — margines z przyciskami

Nowy `IMargin` (obok `LineNumberGutter`):

```cpp
class FoldingGutter : public IMargin {
public:
    FoldingGutter(const FoldState* state);
    int  preferredWidth(const ViewportState& vp) const override;
    void paint(QPainter& p, const ViewportState& vp, const QRect& rect) override;

    // Opcjonalnie: kliki obsługuje Rail, przekazując do FoldingGutter
    void mousePress(const QPoint& local, int clickedLine);
signals:
    void toggleRequested(int regionIndex);
};
```

### 7.1 Rysowanie

Dla każdej widocznej visual-row (używamy `vp.rows`):

- jeśli row jest `isFirstRow` i na tej `logicalLine` zaczyna się region:
  - rysuj **`▾`** (zwijalny, rozwinięty) lub **`▸`** (zwinięty) — 10×10 px
- wpp. jeśli row jest wewnątrz rozwiniętego regionu (depth > 0):
  - rysuj pionową linię w środku gutter-a, kolor `QPalette::Mid`
  - na ostatniej row-ie regionu: róg (pionowa + pozioma kreska w prawo)

Na odpowiedniej głębokości zagnieżdżenia pionowe linie są rysowane *jedna obok drugiej* od lewej (depth 0, 1, 2…) — identycznie jak w VS Code / JetBrains.

### 7.2 Klik

- `mousePress` → liczymy `logicalLine` z współrzędnych → `regionStartingAt(line)` → emit `toggleRequested(regionIndex)`.
- `CodeEditArea` łapie sygnał i wywołuje `m_foldState.toggle(regionIndex)`, potem rebuilduje wrap layout i odświeża.

---

## 8. API użytkownika (końcowe)

### 8.1 Publiczne nagłówki

```
include/qce/
    FoldRegion.h          (struct FoldRegion)
    IFoldingProvider.h    (class IFoldingProvider, CompositeFoldingProvider)
    RuleBasedFoldingProvider.h
    SemanticFoldingProvider.h   (helper do dziedziczenia)
    FoldState.h           (class FoldState)
    margins/FoldingGutter.h
```

### 8.2 `CodeEditArea`

```cpp
class CodeEditArea : public QAbstractScrollArea {
public:
    void setFoldingProvider(IFoldingProvider* p);  ///< non-owning
    IFoldingProvider* foldingProvider() const;

    // Publiczne operacje foldingu:
    void foldRegion(int regionIndex);
    void unfoldRegion(int regionIndex);
    void toggleFoldAt(int line);      ///< region zaczynający się w `line`
    void foldAll();
    void unfoldAll();
    void foldToLevel(int level);

signals:
    void foldStateChanged();
};
```

### 8.3 Użycie w demo (docelowe)

```cpp
// KateXmlReader::load w demo zwraca PARĘ: highlighter + folding provider
auto [hl, fold] = KateXmlReader::load(".../c.xml", theme);

editor->area()->setHighlighter(hl.get());
editor->area()->setFoldingProvider(fold.get());

// Opcjonalny custom:
auto composite = std::make_unique<qce::CompositeFoldingProvider>();
composite->add(std::move(fold));
composite->add(std::make_unique<DotCommentGroupFolder>());
editor->area()->setFoldingProvider(composite.get());

// Gutter:
auto gutter = std::make_unique<qce::FoldingGutter>(&editor->area()->foldState());
editor->addLeftMargin(gutter.get());
```

### 8.4 Klawisze

| Skrót | Akcja |
|---|---|
| `Ctrl+-` | zwiń region zawierający kursor |
| `Ctrl++` / `Ctrl+=` | rozwiń region zaczynający się/zawierający kursor |
| `Ctrl+Shift+-` | zwiń wszystkie |
| `Ctrl+Shift++` | rozwiń wszystkie |
| `Ctrl+1..9` | zwiń do poziomu 1..9 |

---

## 9. Testy

### 9.1 Testy jednostkowe `RuleBasedFoldingProvider`

Programowany highlighter z dwoma regułami `{`/`}` na region `Brace1`:

```cpp
doc.setText("int foo() {\n    return 1;\n}\nint bar() {}\n");
auto regions = provider.computeRegions(&doc);
QCOMPARE(regions.size(), 2);
QCOMPARE(regions[0].startLine, 0);  // "int foo() {"
QCOMPARE(regions[0].endLine, 2);    // "}"
QCOMPARE(regions[1].startLine, 3);  // same-line, ignored? → NO, samelln zachowujemy jeśli wielo-kolumnowy
```

### 9.2 Testy `FoldState`

- `isLineVisible` dla zagnieżdżonych regionów: zewnętrzny zwinięty → wszystkie wewnątrz ukryte.
- `foldToLevel(1)` zwija tylko depth=0.
- Rebuild z zachowaniem stanu: stary `{start=5, group="curly"}` zwinięty; po edycji nowy `{start=7, group="curly"}` też zwinięty (heurystyczne przeniesienie).

### 9.3 Integracyjne

- Kate c.xml → demo z rzeczywistym plikiem .c. Sprawdź że `/* ... */` i `{ ... }` się zwijają.

---

## 10. Etapy implementacji

| Etap | Zakres | Tag |
|---|---|---|
| **A** | Model danych: `FoldRegion`, `IFoldingProvider`, `FoldState`, `CompositeFoldingProvider` (bez renderingu) | — |
| **B** | `RuleBasedFoldingProvider` — wymaga istniejącego `RulesHighlighter` z `beginRegionId`/`endRegionId` w regułach; algorytm stack-based | — |
| **C** | Integracja z `WrapLayout` — `lineFirstRow[i]=-1` dla ukrytych linii; `visualRowOf` pomija ukryte | — |
| **D** | Placeholder rendering w `LineRenderer` | **tag `v0.9.0` — folding widoczny w demo** |
| **E** | `FoldingGutter` (IMargin) z przyciskami ▸/▾ i pionowymi liniami | — |
| **F** | Klawisze i API publiczne (`foldRegion`, `foldAll`, `foldToLevel`) | — |
| **G** | `SemanticFoldingProvider` + przykład `CommentGroupFolder` | — |
| **H** | Rebuild z zachowaniem stanu zwinięć po edycji | **tag `v1.0.0` — pełny folding** |

---

## 11. Co wzięliśmy skąd

| Koncept | Źródło |
|---|---|
| Markery `beginRegion`/`endRegion` na regułach highlightera | Kate XML |
| Pairing po nazwie (`Brace1` + `Brace1`), stos w trakcie parsowania | Kate silnik |
| `placeholder` z konfigurowalnym tekstem | JetBrains `FoldingDescriptor` |
| `collapsedByDefault` per region | JetBrains |
| `FoldingProvider` jako warstwa niezależna od składni | JetBrains `FoldingBuilderEx` |
| Grupowanie elementów w jeden region (komentarze, importy) | JetBrains (`DotFoldingBuilder.foldBlock`) |
| `DotFoldingSettings` — użytkownik wybiera typy fold-ów | JetBrains |
| Kolumna `▸`/`▾` i pionowe linie zagnieżdżenia | VS Code / JetBrains (wizualnie identyczne) |
| Brak folding w core — core przyjmuje dane | decyzja projektowa qcodeedit |

---

## 12. Co pomijamy w v1 (TODO)

- **Inline fold** (zwinięcie fragmentu *wewnątrz* jednej linii) — wymaga rysowania placeholder w środku linii i dzielenia rendering na segmenty-przed / placeholder / segmenty-po. Dużo zachodu, mało wartości.
- **Animacja** zwijania/rozwijania. Nie potrzebne funkcjonalnie.
- **Persistence** stanu zwinięć między sesjami. Trywialne do dorobienia (serializacja `FoldState` — indexy regionów + identyfikatory semantyczne) ale nie w core.
- **Fold-guides** (subtelne kropki głębokości w treści, nie w gutterze) — VS Code-style. Kosmetyka, nie funkcja.
- **Custom fold w komentarzach** (`/*region Foo*/ … /*endregion*/`). To rozszerzenie rule-based providera (dodatkowe nazwane regiony od komentarzy) — można dodać w `KateXmlReader` po v1.
