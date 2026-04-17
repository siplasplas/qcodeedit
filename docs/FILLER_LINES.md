# Filler Lines — Design Document

Cel: obsłużyć *puste wiersze wizualne*, które **nie są** liniami dokumentu. WinMerge / Meld / DiffMerge / Beyond Compare wstawiają takie wiersze, aby w widoku side-by-side lewy i prawy panel miały **równoległe linie** — gdy w jednym pliku są linie, których w drugim nie ma, w drugim wstawiamy tyle "pustych zastępników" ile trzeba.

Filler wiersze:

- **nie są** numerowane (gutter nie drukuje numeru),
- nie uczestniczą w nawigacji kursora (kursor je przeskakuje w górę/dół),
- mają własne tło (klasycznie: zielone, czerwone, niebieskie dla added / removed / changed),
- liczą się do zakresu pionowego scrollbara (żeby pionowe skrolowanie dwóch edytorów było synchronizowalne 1:1).

Konsekwentnie z architekturą syntax highlightingu i folding: **core qcodeedit** zna model danych i rysuje, ale **nie** oblicza które linie są filler — to robi zewnętrzna aplikacja (np. widget DiffMerge) i przekazuje przez API.

---

## 1. Scenariusz: DiffMerge side-by-side

Dwa edytory, każdy z własnym dokumentem:

```
LEFT (10 lines)                      RIGHT (8 lines)
─────────────────────              ─────────────────────
  1  void foo() {                    1  void foo() {
  2    int a = 1;                    2    int a = 1;
  3    int b = 2;                    3    int b = 2;
  4  -- added in LEFT --             4  -- filler (green)  --
  5  -- added in LEFT --             5  -- filler (green)  --
  6    return a + b;                 6    return a + b;
  7  }                               7  }
  8                                  8
  9  int bar = 0;                    9  (no content)
 10                                 10  (no content)
─────────────────────              ─────────────────────
```

Aby linie `return a + b;` były dokładnie naprzeciw siebie (na tej samej wysokości w pikselach), w prawym edytorze trzeba wstawić 2 filler wiersze przed jego linią 4. Oba edytory współdzielą scrollbar (DiffMerge trzyma synchronizację) — `verticalScrollBar().value()` interpretowany jest jako „pozycja visual row", a że suma wszystkich visual rows w obu panelach jest równa, synchronizacja działa bez kombinacji.

Analogicznie na końcu — LEFT ma `int bar = 0;` przy linii 9, RIGHT kończy się na linii 7, więc RIGHT dostaje 2 fillery na końcu.

---

## 2. Architektura

```
┌──────────────────────────────────────────────────────────────┐
│  APLIKACJA (np. DiffMerge side-by-side widget)               │
│                                                              │
│  - uruchamia diff algorithm                                  │
│  - produkuje QVector<FillerLine> dla LEFT i RIGHT            │
│  - woła leftArea->setFillerState(leftFillers)                │
│          rightArea->setFillerState(rightFillers)             │
│  - synchronizuje scrollbar obu edytorów                      │
└──────────────────────────────────────────────────────────────┘
                         │
                         │ setFillerState(FillerState*)
                         ▼
┌──────────────────────────────────────────────────────────────┐
│  CORE qcodeedit                                              │
│                                                              │
│  FillerLine / FillerState   — publiczny model danych         │
│                                                              │
│  WrapLayout                 — widzi FillerState + FoldState  │
│                               i produkuje sekwencję visual   │
│                               rows z wplecionymi fillerami   │
│                                                              │
│  ViewportState::RowInfo     — zyskuje isFiller / fillerColor │
│                               / fillerLabel                  │
│                                                              │
│  LineRenderer               — filler rysowany jako fillRect  │
│                               (+ opcjonalny label)           │
│                                                              │
│  LineNumberGutter           — pomija fillery                 │
│  FoldingGutter              — pomija fillery                 │
│                                                              │
│  cursorFromPoint            — klik w filler → najbliższa     │
│                               widoczna linia dokumentu       │
│                                                              │
│  CaretPainter               — filler row nigdy nie trzyma    │
│                               kursora (kursor zawsze na      │
│                               logical line)                  │
└──────────────────────────────────────────────────────────────┘
```

---

## 3. Model danych (publiczne API)

### 3.1 `FillerLine`

```cpp
namespace qce {

/// A block of filler rows inserted before a specific logical line.
struct FillerLine {
    /// Insert this block immediately BEFORE this logical line. Use
    /// doc->lineCount() to append at the very bottom.
    int     beforeLine = 0;

    /// How many visual rows this filler block occupies.
    int     rowCount   = 1;

    /// Solid background for the entire filler block. Invalid QColor =
    /// use editor's default base colour (rarely useful; normally you set
    /// an added/removed/changed tint here).
    QColor  fillColor;

    /// Optional centred text shown on the first filler row (e.g. "no match",
    /// "25 lines omitted"). Empty = nothing drawn.
    QString label;
};

} // namespace qce
```

### 3.2 `FillerState`

Kontener po stronie edytora. Analogiczny do `FoldState`, trzymany przez `CodeEditArea`, wypełniany przez aplikację.

```cpp
namespace qce {

class FillerState {
public:
    void setFillers(QVector<FillerLine> fillers);
    const QVector<FillerLine>& fillers() const;

    /// Total number of filler rows across the whole document — used by the
    /// scrollbar range calculation.
    int totalFillerRows() const;

    /// Sum of rowCount of all filler blocks whose beforeLine <= logicalLine.
    /// Used to map (logical line) → (global visual row) and vice versa.
    int fillerRowsBefore(int logicalLine) const;

    /// Find a block by global row index range. Returns -1 if index is not
    /// within any filler block.
    int fillerBlockAtGlobalRow(int globalRow) const;
};

} // namespace qce
```

Normalizacja wewnątrz `setFillers()`:

- `rowCount <= 0` i `beforeLine < 0` → blok odrzucany,
- fillery sortowane po `beforeLine`,
- dwa bloki z tym samym `beforeLine` są scalane (sumuje `rowCount`, priorytet koloru: pierwszy niezerowy).

### 3.3 `CodeEditArea` — public API

```cpp
class CodeEditArea : public QAbstractScrollArea {
    // ...
public:
    /// Aplikacja buduje i przechowuje FillerState. CodeEditArea dostaje
    /// non-owning wskaźnik; null oznacza "bez filler-ów" (zachowanie
    /// domyślne). Po każdej zmianie contents FillerState aplikacja woła
    /// setFillerState(fs) ponownie (lub wzywa refreshFillers()).
    void setFillerState(FillerState* fs);
    FillerState* fillerState() const;

    /// Convenience: po zmianie zawartości m_fillerState, rebuild layout
    /// i repaint. Aplikacja może zamiast tego po prostu wywołać setFillerState
    /// ponownie.
    void refreshFillers();
};
```

---

## 4. Integracja z `WrapLayout`

`WrapLayout` już dziś przyjmuje `const FoldState* foldState` i filtruje ukryte linie. Dodajemy drugi opcjonalny parametr:

```cpp
void WrapLayout::rebuild(const ITextDocument* doc,
                         int availableVisualCols,
                         int tabWidth,
                         const FoldState* foldState = nullptr,
                         const FillerState* fillerState = nullptr);
```

### 4.1 Poszerzenie `Row`

```cpp
struct WrapLayout::Row {
    int logicalLine = -1;   ///< -1 dla filler rows
    int startCol    = 0;
    int endCol      = 0;
    // Nowe, wypełniane tylko dla filler:
    int fillerBlockIndex = -1;   ///< indeks do FillerState::fillers()
    // (kolor / label bierzemy na żądanie z FillerState — nie duplikujemy)
};
```

### 4.2 Algorytm (nowa wersja)

```
rows = []
lineFirstRow = []
for li = 0..lineCount-1:
    // 1. Filler block PRZED linią li
    if fillerState:
        for fb in fillerState.blocksBefore(li):
            for k = 0..fb.rowCount-1:
                rows.append({logicalLine=-1, fillerBlockIndex=fb.index})
    // 2. Sama linia (lub pominięta przez fold)
    if foldState && !foldState.isLineVisible(li):
        lineFirstRow.append(lastVisibleFirstRow)
        continue
    lastVisibleFirstRow = rows.size()
    lineFirstRow.append(lastVisibleFirstRow)
    // ... normalne wrap rows dla linii li ...
// 3. Filler block z beforeLine == lineCount (końcowy "ogon")
if fillerState:
    for fb in fillerState.blocksBefore(lineCount+1) where fb.beforeLine >= lineCount:
        for k = 0..fb.rowCount-1:
            rows.append({logicalLine=-1, fillerBlockIndex=fb.index})
```

**Uwaga**: filler blok przed linią ukrytą przez fold — zachowujemy filler (jest on „obok" dokumentu, nie „w" nim). Albo pomijamy go wraz z ukrytą linią? Dla DiffMerge MVP zachowujemy, bo fold rzadko współwystępuje z diff.

---

## 5. `ViewportState` — rozszerzenie

Rozszerzamy `RowInfo`:

```cpp
struct ViewportState::RowInfo {
    int  logicalLine;     // -1 = filler row
    int  startCol, endCol;
    bool isFirstRow;
    QString foldPlaceholder;
    int     foldStartColumn = -1;

    // Nowe:
    bool    isFiller = false;
    QColor  fillerColor;
    QString fillerLabel;    ///< niepusty tylko na pierwszej row bloku
};
```

`CodeEditArea::refreshViewportState()` wypełnia nowe pola przy mapowaniu `WrapLayout::Row` → `ViewportState::RowInfo`. Gdy `wrapRow.logicalLine == -1`:

- `ri.isFiller = true`,
- `ri.logicalLine = -1`,
- `ri.fillerColor = fillerState->fillers()[wrapRow.fillerBlockIndex].fillColor`,
- `ri.fillerLabel = (firstRowOfBlock) ? ...fillerLabel : ""`.

`vp.firstVisibleLine` / `vp.lastVisibleLine` zyskują semantykę: pierwszy / ostatni **niefillerowy** logical line w aktualnym widoku. Używane przez LineNumberGutter w starej ścieżce (bez `vp.rows`).

---

## 6. Rendering

### 6.1 `LineRenderer::paint()` — nowa gałąź

W obecnej pętli po `vp.rows`:

```cpp
for (int ri = 0; ri < vp.rows.size(); ++ri) {
    const auto& row = vp.rows[ri];
    const int topY = vp.contentOffsetY + ri * lineHeight;

    if (row.isFiller) {
        // 1) tło całej szerokości
        if (row.fillerColor.isValid()) {
            painter.fillRect(0, topY, vp.viewportWidth, lineHeight, row.fillerColor);
        }
        // 2) opcjonalny wyśrodkowany label
        if (!row.fillerLabel.isEmpty()) {
            painter.save();
            QColor c = painter.pen().color();
            c.setAlphaF(0.55);
            painter.setPen(c);
            const QFontMetrics fm = painter.fontMetrics();
            const int x = (vp.viewportWidth - fm.horizontalAdvance(row.fillerLabel)) / 2;
            painter.drawText(x, topY + fm.ascent(), row.fillerLabel);
            painter.restore();
        }
        continue;  // nie rysujemy spans / placeholderów
    }
    // ... istniejąca ścieżka (content row) ...
}
```

### 6.2 `paintLineBackgrounds` i `paintSelection`

Filler rows nie podlegają `m_lineBgProvider` ani selekcji. W `paintLineBackgrounds`:

```cpp
for (int i = 0; i < vp.rows.size(); ++i) {
    if (vp.rows[i].isFiller) continue;  // filler ma własny color w paint()
    // ...
}
```

W `selectionRegion()` — filler rows są pomijane (selekcja przechodzi NAD filler-em wizualnie tylko na content rows; w wrap mode selekcja od linii X do Y generuje rects tylko dla content rows w tym zakresie).

---

## 7. Margines (`LineNumberGutter`, `FoldingGutter`)

Oba skip filler:

```cpp
for (int i = 0; i < vp.rows.size(); ++i) {
    if (vp.rows[i].isFiller) continue;
    // ... rysuj numer / arrow ...
}
```

Gutter może też **rysować tło filler** w swoim pasku — dzięki temu kolor bloku rozciąga się na cały wiersz łącznie z gutterem. Wtedy w LineNumberGutter:

```cpp
if (row.isFiller) {
    if (row.fillerColor.isValid()) {
        painter.fillRect(marginRect.left(), topY, marginRect.width(),
                         vp.lineHeight, row.fillerColor);
    }
    continue;
}
```

Tak samo w `FoldingGutter` — filler rows mają tło zlane z viewportem.

---

## 8. Cursor / mouse

### 8.1 `cursorFromPoint`

Klik w filler row → kursor ląduje na **najbliższej widocznej** logical line. Prosty algorytm:

```cpp
if (row.isFiller) {
    // Znajdź najbliższą content row, preferując tę POWYŻEJ filler-a.
    int j = rowIndex - 1;
    while (j >= 0 && vp.rows[j].isFiller) --j;
    if (j >= 0) {
        return {vp.rows[j].logicalLine, vp.rows[j].endCol};
    }
    // Fallback: pierwsza content row poniżej.
    j = rowIndex + 1;
    while (j < vp.rows.size() && vp.rows[j].isFiller) ++j;
    if (j < vp.rows.size()) {
        return {vp.rows[j].logicalLine, 0};
    }
}
```

### 8.2 Nawigacja strzałkami

`CursorController` operuje na (line, col) **logicznych**, nie wie o filler-ach. Strzałki Up/Down mogą korzystać z mapowania przez `WrapLayout::rowForCursor()` i `visualRowOf()` — fillery w tym mapowaniu reprezentują "martwe" wiersze i algorytm nawigacji naturalnie je przeskakuje (bo po wyliczeniu targetu visual-row wracamy do kontekstowego mapowania na logical, które ignoruje filler rows).

Tu warto dodać test pokrywający case: kursor na ostatniej linii PRZED filler-em, Down → kolejna content row (pomija filler). Pokrycie w unit testach dla `CursorController` / `CodeEditArea::keyPressEvent`.

### 8.3 `ensureCursorVisible`

Bez zmian — operujemy na `visualRowOf(cursor)` czyli globalnym indeksie row, który uwzględnia filler-y.

---

## 9. Scrollbar

`updateScrollBarRanges()` — `vMax = totalVisualRows - pageRows`, gdzie `totalVisualRows` to teraz `m_wrapLayout->totalRows()` (zawiera już wplecione filler rows).

**Brak innych zmian**. Sam mechanizm synchronizacji dwóch edytorów w DiffMerge to aplikacja podpina sygnały `QScrollBar::valueChanged` — core nie musi nic wiedzieć.

---

## 10. Testy

### 10.1 `FillerState`

- `totalFillerRows` sumuje `rowCount`,
- `fillerRowsBefore(L)` dla bloków przed L,
- `setFillers` normalizuje: scalenie duplikatów, odrzucenie pustych, sort po `beforeLine`.

### 10.2 `WrapLayout` + filler

- Document o 3 liniach, filler `{beforeLine=1, rowCount=2}` → 5 row-ów, row 1 i 2 to filler.
- Filler na końcu `{beforeLine=3, rowCount=1}` → row 3 to filler.
- `rowCountOf(lineIdx)` nadal liczy tylko content rows.
- Filler współpracuje z word-wrap (linia z wrap-em + filler przed/po).
- Filler i fold jednocześnie: linia 2 foldowana + filler przed linią 2 → filler rows widoczne, linia 2 content ukryta.

### 10.3 Rendering — smoke

Test pikselowy (mały QImage): fillRect pod filler, brak tekstu, label centred.

### 10.4 `cursorFromPoint` smoke

Klik Y trafiający w filler row → kursor na linii tuż powyżej.

---

## 11. Demo (opcjonalne — dla side-by-side widoku osobne demo)

Zrobienie pełnego diff UI wykracza poza qcodeedit core. Dla zademonstrowania filler-ów można dodać menu `Demo > Insert sample fillers`, które ustawia statyczne:

```cpp
m_fillerState.setFillers({
    qce::FillerLine{7,  2, QColor("#D4F4DD"), QStringLiteral("added in other file")},
    qce::FillerLine{14, 1, QColor("#FBDADA"), QStringLiteral("removed in this file")},
});
m_editor->area()->setFillerState(&m_fillerState);
```

Po aktywowaniu między liniami widać pasy w dwóch kolorach — wizualnie potwierdza że mechanizm działa w tym samym viewporcie co linie numerów i fold arrows.

Całościowe side-by-side demo najlepiej zbudować w osobnym, pochodnym projekcie (np. `qcodeedit-diffmerge`), który komponuje dwa `CodeEdit` + wspólny scrollbar + engine diff.

---

## 12. Etapy implementacji

| Etap | Zakres | Tag |
|---|---|---|
| **A** | `FillerLine`, `FillerState` (+ testy jednostkowe) | — |
| **B** | `WrapLayout::rebuild` dostaje `FillerState*`; nowy `Row` z `logicalLine=-1` dla filler; `totalRows` zawiera filler | — |
| **C** | `ViewportState::RowInfo` rozszerzone; `CodeEditArea::refreshViewportState` mapuje filler-y | — |
| **D** | `LineRenderer` rysuje filler (tło + opcjonalny label); `paintLineBackgrounds` i selekcja pomijają filler | — |
| **E** | `LineNumberGutter` i `FoldingGutter` pomijają filler; opcjonalnie rozszerzają tło filler-a na swój pasek | — |
| **F** | `cursorFromPoint` + `CursorController` skip filler; click-test i nawigacja | **tag `v1.1.0` — filler MVP** |
| **G** | Sync-scrollbar helper (pomocnik klasy, opcjonalny) i demo side-by-side jako osobny projekt | — |

Etap G wykracza poza qcodeedit — celem byłoby udostępnić demo na bazie dwóch `CodeEdit` łączących się przez zewnętrzny widget synchronizujący pionowy scroll oraz obliczający diff.

---

## 13. Co świadomie pomijamy (v1)

- **Split-color filler** (np. lewa połowa zielona, prawa czerwona) — wymagałby dwóch fillColor albo gradient; obecnie jedna `fillColor` na blok.
- **Interaktywne fillery** (klik → skocz do odpowiadającej linii w drugim panelu) — to poziom aplikacji, nie core.
- **Animowane odsłanianie fillerów** — zbędne.
- **Filler jako osobny widget** — celowo trzymamy ich w tym samym viewporcie co tekst; w DiffMerge dwie strony po prostu mają dwa osobne edytory z różnymi filler-ami.
