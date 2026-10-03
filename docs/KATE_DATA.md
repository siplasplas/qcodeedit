# Dane Kate: definicje składni i motywy

qcodeedit sam zarządza plikami Kate (`*.xml` z definicjami składni i
`*.theme`), zamiast polegać na katalogu KDE. Dzięki temu każdy edytor oparty na
qcodeedit dostaje ten sam, sprawdzony zestaw danych bez powielania kodu.

## Biblioteki

| cel CMake              | zawartość                                                      | zależności          |
|------------------------|----------------------------------------------------------------|---------------------|
| `qcodeedit::qcodeedit` | komponent edycji (nie zna Kate)                                | Qt Core/Gui/Widgets |
| `qcodeedit::kate`      | `KateXmlReader`, `KateTheme`, wersja, ścieżki, `KateSyntaxIndex` | + Qt Core           |
| `qcodeedit::katedata`  | `KateDataDownloader`, narzędzie `qce-kate-fetch`               | + **Qt Network**    |

`katedata` jest opcjonalne (`-DQCE_BUILD_KATE_DOWNLOADER=OFF`). Bez niego
Qt Network nie jest potrzebne.

## Wersja zestawu

```cpp
#include <qce/kate/KateSyntaxVersion.h>

constexpr auto v = qce::kate::supportedSyntaxVersion();  // {6, 31}
v.toString();        // "6.31"
v.updateFileName();  // "update-6.31.xml"
v.updateUrl();       // "https://kate-editor.org/syntax/update-6.31.xml"
```

To wersja, na której sprawdzono `KateXmlReader`. Zmienia się ją **ręcznie**
po sprawdzeniu parsera na nowszym zestawie (`tools/scan_unknown_attrs.py` i
testy regresji).

## Katalogi

```cpp
#include <qce/kate/KatePaths.h>

qce::kate::dataRoot();   // GenericDataLocation + "/qcodeedit"
qce::kate::dataDir();    // dataRoot() + "/kate-6.31"
qce::kate::syntaxDir();  // dataDir() + "/syntax"
qce::kate::themesDir();  // dataDir() + "/themes"
```

| system  | `dataDir()`                                         |
|---------|-----------------------------------------------------|
| Linux   | `~/.local/share/qcodeedit/kate-6.31`                |
| Windows | `C:/Users/<user>/AppData/Local/qcodeedit/kate-6.31` |
| macOS   | `~/Library/Application Support/qcodeedit/kate-6.31` |

Kolejność ustalania `dataDir()`: `setDataDirOverride()`, potem zmienna
`QCE_KATE_DATA_DIR`, na końcu wartość domyślna. Override podaje gotowy
katalog, bez dopisywania wersji.

Każda wersja ma osobny katalog. Po podbiciu wersji wszystko pobiera się od
nowa, bez kopiowania ze starego katalogu. Stare katalogi `kate-*` usuwa się
ręcznie; biblioteka ich nie rusza.

Zawartość:

```
kate-6.31/
  index.json        indeks (KateSyntaxIndex)
  update-6.31.xml   lista definicji z ostatniego pobrania
  theme-data.qrc    lista motywów z ostatniego pobrania
  syntax/*.xml
  themes/*.theme
```

## Indeks: `KateSyntaxIndex`

```cpp
#include <qce/kate/KateSyntaxIndex.h>

auto index = qce::kate::KateSyntaxIndex::load(qce::kate::dataDir());
index.saveIfDirty();

for (const auto& e : index.languages())  // nazwa, sekcja, version, kateversion, …
    ;
auto matches = index.forFileName("src/main.cpp");   // według priorytetu
if (!matches.isEmpty()) {
    const QString path = index.filePath(*matches.first());
    auto hl = KateXmlReader::load(path, theme, index);  // ##Lang przez indeks
}
```

- `index.json` jest przepisywany w całości (`QSaveFile`).
- `load()` ponownie czyta tylko pliki o innym rozmiarze lub mtime, i to tylko
  nagłówek `<language>`. Nieaktualny lub uszkodzony JSON oznacza przebudowę.
- `unsupported: true` oznacza, że `kateversion` jest wyższy niż
  `supportedSyntaxVersion()`. `forFileName()` domyślnie takie pliki pomija.
- `invalidate(file)` wymusza ponowne odczytanie pliku nadpisanego w miejscu
  (znaczniki czasu plików mają rozdzielczość kilku ms).

## Pobieranie: `KateDataDownloader`

```cpp
#include <qce/kate/KateDataDownloader.h>

auto* dl = new qce::kate::KateDataDownloader(this);
connect(dl, &qce::kate::KateDataDownloader::progress, this, [](int done, int total) { … });
connect(dl, &qce::kate::KateDataDownloader::finished, this,
        [](bool ok, int downloaded, int failed) { … });
if (dl->mustDownload())   // offline, tani test
    dl->start();
```

- Syntax: `update-6.31.xml` z kate-editor.org; pobierane są brakujące pliki
  i te z wyższą `version` niż w indeksie.
- Motywy: `theme-data.qrc` z tagu `v6.31.0`, a jeśli go nie ma (wersja
  jeszcze niewydana), z `master`. Pobierane są tylko brakujące.
- Pliki są zapisywane atomowo i tylko po udanym pobraniu. Nazwy z sieci są
  sprawdzane, więc nic nie trafia poza katalog danych.
- Klasa nie pokazuje żadnych okien; o momencie pobierania i interfejsie
  decyduje aplikacja.

Z linii poleceń:

```bash
qce-kate-fetch            # pobiera do qce::kate::dataDir()
qce-kate-fetch --check    # exit 0 = aktualne, 1 = trzeba pobrać
qce-kate-fetch --dir /tmp/kate-data
```

## Testy

Testy nigdy nie łączą się z siecią: downloader jest testowany przez adresy
`file://`. Testy na prawdziwych definicjach szukają plików w `syntaxDir()`, a
przejściowo także w `~/.local/share/org.kde.syntax-highlighting/syntax`
(`tests/KateTestPaths.h`). Jeśli niczego nie znajdą, robią `QSKIP`.
