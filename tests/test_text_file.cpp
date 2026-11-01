#include <qce/encoding/TextFile.h>

#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include <algorithm>

using namespace qce;
using namespace qce::encoding;

// "Zażółć gęślą jaźń" in cp1250: ż=BF, ó=F3, ł=B3, ć=E6, ę=EA, ś=9C, ą=B9, ź=9F, ń=F1.
static const QByteArray kPolishCp1250("Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1.");
static const QString kPolish = QStringLiteral("Zażółć gęślą jaźń.");

// An area showing a document, with the file behind it.
struct Editor {
    SimpleTextDocument document;
    CodeEditArea area;
    TextFile file{&area, &document};
    Editor() { area.setDocument(&document); }
};

class TestTextFile : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& bytes) {
        const QString path = m_dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write(bytes);
        return path;
    }

    static QByteArray read(const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void opensUtf8() {
        const QString path = write("utf8.txt", "alpha\nbeta\n");
        Editor editor;
        QSignalSpy pathChanged(&editor.file, &TextFile::pathChanged);
        QSignalSpy loaded(&editor.file, &TextFile::loaded);
        QCOMPARE(editor.file.open(path), TextFile::Result::Ok);
        QCOMPARE(editor.document.toPlainText(), QStringLiteral("alpha\nbeta"));
        QCOMPARE(editor.file.path(), path);
        QCOMPARE(editor.file.encoding(), QStringLiteral("utf8"));
        QVERIFY(editor.file.format().finalNewline);
        QVERIFY(!editor.file.isModified());
        QVERIFY(editor.file.uncertainDetection().options.isEmpty());
        QCOMPARE(pathChanged.count(), 1);
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(loaded.first().first().toByteArray(), QByteArray("alpha\nbeta\n"));
    }

    void savesBackAsRead() {
        const QByteArray bytes = kPolishCp1250 + "\r\nsecond\r\n";
        const QString path = write("cp1250.txt", bytes);
        Editor editor;
        QCOMPARE(editor.file.open(path, QStringLiteral("cp1250")), TextFile::Result::Ok);
        QCOMPARE(editor.document.toPlainText(), kPolish + QStringLiteral("\nsecond"));
        QVERIFY(editor.file.format().crlf);
        QCOMPARE(editor.file.save(), TextFile::Result::Ok);
        QCOMPARE(read(path), bytes);
    }

    void newFileIsSavedThere() {
        const QString path = m_dir.filePath("new.txt");
        Editor editor;
        QCOMPARE(editor.file.open(path), TextFile::Result::Ok);
        QCOMPARE(editor.document.toPlainText(), QString());
        QCOMPARE(editor.file.path(), path);
        editor.area.insertText(QStringLiteral("text"));
        QVERIFY(editor.file.isModified());
        QCOMPARE(editor.file.save(), TextFile::Result::Ok);
        QCOMPARE(read(path), QByteArray("text\n"));
        QVERIFY(!editor.file.isModified());
    }

    void reportsModification() {
        const QString path = write("modified.txt", kPolishCp1250 + "\n");
        Editor editor;
        QCOMPARE(editor.file.open(path, QStringLiteral("cp1250")), TextFile::Result::Ok);
        QSignalSpy modified(&editor.file, &TextFile::modificationChanged);
        QSignalSpy formatChanged(&editor.file, &TextFile::formatChanged);
        // Another encoding to save in is a change, though not an undo step.
        editor.file.setSaveEncoding(QStringLiteral("utf8"));
        QVERIFY(editor.file.isModified());
        QCOMPARE(modified.count(), 1);
        QCOMPARE(modified.last().first().toBool(), true);
        QCOMPARE(formatChanged.count(), 1);
        QCOMPARE(editor.file.save(), TextFile::Result::Ok);
        QVERIFY(!editor.file.isModified());
        QCOMPARE(modified.last().first().toBool(), false);
        QCOMPARE(read(path), (kPolish + QStringLiteral("\n")).toUtf8());
    }

    void opensWithoutReportingModification() {
        const QString path = write("plain.txt", kPolishCp1250 + "\n");
        Editor editor;
        QSignalSpy modified(&editor.file, &TextFile::modificationChanged);
        QCOMPARE(editor.file.open(path, QStringLiteral("cp1250")), TextFile::Result::Ok);
        QCOMPARE(editor.file.reopen(QStringLiteral("iso-8859-2")), TextFile::Result::Ok);
        QVERIFY(!editor.file.isModified());
        QCOMPARE(modified.count(), 0);
    }

    void detectsExternalChanges() {
        const QString path = write("external.txt", "one\n");
        Editor editor;
        QCOMPARE(editor.file.open(path), TextFile::Result::Ok);
        QVERIFY(!editor.file.hasExternalChanges());
        write("external.txt", "one two\n");
        QVERIFY(editor.file.hasExternalChanges());
        editor.file.ignoreExternalChanges();
        QVERIFY(!editor.file.hasExternalChanges());
        QCOMPARE(editor.file.reload(), TextFile::Result::Ok);
        QCOMPARE(editor.document.toPlainText(), QStringLiteral("one two"));
        QVERIFY(!editor.file.isModified());
    }

    void filtersBytes() {
        auto reverse = [](const QString&, QByteArray* bytes) {
            std::reverse(bytes->begin(), bytes->end());
            return TextFile::Result::Ok;
        };
        const QString path = write("reversed.txt", "\ncba");
        Editor editor;
        editor.file.setReadFilter(reverse);
        editor.file.setWriteFilter(reverse);
        QSignalSpy loaded(&editor.file, &TextFile::loaded);
        QCOMPARE(editor.file.open(path), TextFile::Result::Ok);
        QCOMPARE(editor.document.toPlainText(), QStringLiteral("abc"));
        QCOMPARE(loaded.first().first().toByteArray(), QByteArray("abc\n"));
        editor.area.insertText(QStringLiteral("x"));
        QCOMPARE(editor.file.save(), TextFile::Result::Ok);
        QCOMPARE(read(path), QByteArray("\ncbax"));

        editor.file.setWriteFilter([](const QString&, QByteArray*) { return TextFile::Result::Cancelled; });
        editor.area.insertText(QStringLiteral("y"));
        QCOMPARE(editor.file.save(), TextFile::Result::Cancelled);
        QCOMPARE(read(path), QByteArray("\ncbax"));
        QVERIFY(editor.file.isModified());
    }

    void reportsFailures() {
        Editor editor;
        QCOMPARE(editor.file.save(), TextFile::Result::Failed);
        QVERIFY(!editor.file.errorString().isEmpty());
        QCOMPARE(editor.file.saveAs(m_dir.filePath("missing/dir/file.txt")), TextFile::Result::Failed);
        QVERIFY(!editor.file.errorString().isEmpty());
        QVERIFY(editor.file.path().isEmpty());
    }

    void savedUnderAnotherName() {
        const QString path = write("first.txt", "text\n");
        const QString other = m_dir.filePath("second.txt");
        Editor editor;
        QCOMPARE(editor.file.open(path), TextFile::Result::Ok);
        QSignalSpy saved(&editor.file, &TextFile::saved);
        QSignalSpy pathChanged(&editor.file, &TextFile::pathChanged);
        QCOMPARE(editor.file.saveAs(other), TextFile::Result::Ok);
        QCOMPARE(editor.file.path(), other);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.first().at(0).toString(), other);
        QCOMPARE(saved.first().at(1).toString(), path);
        QCOMPARE(pathChanged.count(), 1);
        QCOMPARE(read(other), QByteArray("text\n"));
    }

    void savesMixedLineBreaksAlike() {
        const QString path = write("mixed.txt", "a\r\nb\nc\r\n");
        Editor editor;
        QCOMPARE(editor.file.open(path), TextFile::Result::Ok);
        QVERIFY(editor.file.format().mixedLineBreaks);
        QCOMPARE(editor.file.save(), TextFile::Result::Ok);
        QVERIFY(!editor.file.format().mixedLineBreaks);
        QCOMPARE(read(path), QByteArray("a\r\nb\r\nc\r\n"));
    }

#ifdef QCE_TEST_WITH_DIFF
    void reloadIsAnUndoStep() {
        const QString path = write("reload.txt", "one\ntwo\nthree\n");
        Editor editor;
        QCOMPARE(editor.file.open(path), TextFile::Result::Ok);
        write("reload.txt", "one\n2\nthree\nfour\n");
        QCOMPARE(editor.file.reload(), TextFile::Result::Ok);
        QCOMPARE(editor.document.toPlainText(), QStringLiteral("one\n2\nthree\nfour"));
        QVERIFY(!editor.file.isModified());
        editor.area.undo();
        QCOMPARE(editor.document.toPlainText(), QStringLiteral("one\ntwo\nthree"));
        QVERIFY(editor.file.isModified());
    }

    void trimsChangedLines() {
        const QString path = write("trim.txt", "kept  \nline\n");
        Editor editor;
        QCOMPARE(editor.file.open(path), TextFile::Result::Ok);
        editor.file.setTrimWhitespace(TextFile::TrimWhitespace::ChangedLines);
        editor.area.setCursorPosition({1, 4});
        editor.area.insertText(QStringLiteral(" \t\nlast"));
        QCOMPARE(editor.file.save(), TextFile::Result::Ok);
        // The untouched line keeps its spaces; the cursor's line is not trimmed.
        QCOMPARE(read(path), QByteArray("kept  \nline\nlast\n"));

        editor.file.setTrimWhitespace(TextFile::TrimWhitespace::AllLines);
        editor.area.insertText(QStringLiteral(" "));
        editor.area.setCursorPosition({1, 0});
        QCOMPARE(editor.file.save(), TextFile::Result::Ok);
        QCOMPARE(read(path), QByteArray("kept\nline\nlast\n"));
    }
#endif
};

QTEST_MAIN(TestTextFile)
#include "test_text_file.moc"
