#include <qce/kate/EditorSyntax.h>
#include <qce/kate/KatePaths.h>

#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using qce::kate::EditorSyntax;

// A data directory of its own: one small Kate definition for *.mini files
// and one theme.
class TestEditorSyntax : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_data;

    void write(const QString& relative, const QByteArray& bytes) {
        const QString path = m_data.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    }

private slots:
    void initTestCase() {
        QVERIFY(m_data.isValid());
        qputenv("QCE_KATE_DATA_DIR", m_data.path().toUtf8());
        // Plain literals: moc does not see the class after these as raw strings.
        write("syntax/mini.xml",
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<!DOCTYPE language>\n"
              "<language name=\"Mini\" section=\"Other\" version=\"1\" kateversion=\"5.0\" "
              "extensions=\"*.mini\">\n"
              "  <highlighting>\n"
              "    <contexts>\n"
              "      <context name=\"Normal\" attribute=\"Normal Text\" lineEndContext=\"#stay\">\n"
              "        <DetectChar char=\"#\" attribute=\"Comment\" context=\"Comment\"/>\n"
              "      </context>\n"
              "      <context name=\"Comment\" attribute=\"Comment\" lineEndContext=\"#pop\"/>\n"
              "    </contexts>\n"
              "    <itemDatas>\n"
              "      <itemData name=\"Normal Text\" defStyleNum=\"dsNormal\"/>\n"
              "      <itemData name=\"Comment\" defStyleNum=\"dsComment\"/>\n"
              "    </itemDatas>\n"
              "  </highlighting>\n"
              "</language>\n");
        write("themes/dark.theme",
              "{ \"metadata\": { \"name\": \"Dark\" },\n"
              "  \"editor-colors\": { \"BackgroundColor\": \"#102030\" },\n"
              "  \"text-styles\": { \"Normal\": { \"text-color\": \"#e0e0e0\" } } }");
        qce::kate::reloadSyntaxIndex();
        QVERIFY(qce::kate::syntaxIndex().byFile(QStringLiteral("mini.xml")));
    }

    void choosesByFileName() {
        qce::SimpleTextDocument document;
        qce::CodeEdit edit;
        edit.setDocument(&document);
        EditorSyntax syntax(&edit);
        syntax.setFileName(QStringLiteral("/tmp/a.mini"));
        syntax.apply();
        QVERIFY(edit.area()->highlighter());
        QVERIFY(edit.area()->foldingProvider());

        // No definition and not C or C++: no colours.
        syntax.setFileName(QStringLiteral("/tmp/a.unknown"));
        syntax.apply();
        QVERIFY(!edit.area()->highlighter());
        QVERIFY(!edit.area()->foldingProvider());

        // C++ without a Kate definition: the built-in highlighter.
        syntax.setFileName(QStringLiteral("/tmp/a.cpp"));
        syntax.apply();
        QVERIFY(edit.area()->highlighter());
    }

    void modes() {
        qce::SimpleTextDocument document;
        qce::CodeEdit edit;
        edit.setDocument(&document);
        EditorSyntax syntax(&edit);
        syntax.setFileName(QStringLiteral("/tmp/a.unknown"));
        syntax.setManual(QStringLiteral("mini.xml"));
        QCOMPARE(syntax.mode(), EditorSyntax::Mode::Manual);
        QCOMPARE(syntax.syntaxFile(), QStringLiteral("mini.xml"));
        syntax.apply();
        QVERIFY(edit.area()->highlighter());

        syntax.setPlainText();
        QVERIFY(syntax.syntaxFile().isEmpty());
        syntax.apply();
        QVERIFY(!edit.area()->highlighter());

        syntax.setManual(QStringLiteral("missing.xml"));
        syntax.apply();
        QVERIFY(!edit.area()->highlighter());

        syntax.setAutomatic();
        syntax.setFileName(QStringLiteral("/tmp/b.mini"));
        syntax.apply();
        QVERIFY(edit.area()->highlighter());
    }

    void appliesTheTheme() {
        qce::SimpleTextDocument document;
        qce::CodeEdit edit;
        edit.setDocument(&document);
        EditorSyntax syntax(&edit);
        syntax.setThemeFile(m_data.filePath("themes/dark.theme"));
        syntax.apply();
        QCOMPARE(syntax.theme().name, QStringLiteral("Dark"));
        QCOMPARE(edit.area()->palette().color(QPalette::Base), QColor("#102030"));
        QCOMPARE(edit.area()->palette().color(QPalette::Text), QColor("#e0e0e0"));
    }

    // The editor must not keep the highlighter of a syntax that is gone.
    void detachesWhenDestroyed() {
        qce::SimpleTextDocument document;
        qce::CodeEdit edit;
        edit.setDocument(&document);
        {
            EditorSyntax syntax(&edit);
            syntax.setFileName(QStringLiteral("/tmp/a.mini"));
            syntax.apply();
            QVERIFY(edit.area()->highlighter());
        }
        QVERIFY(!edit.area()->highlighter());
        QVERIFY(!edit.area()->foldingProvider());
    }
};

QTEST_MAIN(TestEditorSyntax)
#include "test_editor_syntax.moc"
