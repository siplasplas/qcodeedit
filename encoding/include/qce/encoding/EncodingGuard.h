#pragma once

#include <qce/encoding/Encoding.h>

#include <QObject>
#include <QPointer>

#include <functional>

class QWidget;

namespace qce {
class CodeEditArea;
}

namespace qce::encoding {

/// Keeps an editor's text storable in its file encoding.
///
/// Attached to a CodeEditArea it checks every typed, pasted or input-method
/// text. When the current encoding (a legacy code page) cannot store some
/// characters, it asks what to do: replace them with '?', switch the
/// document to UTF-8 (it is then saved as UTF-8), or cancel the insertion.
/// The default question is a message box; setChoiceHandler() replaces it.
class EncodingGuard : public QObject {
    Q_OBJECT
public:
    enum class Choice { Replace, SwitchToUtf8, Cancel };

    /// Installs itself as `area`'s insert filter (replacing any other).
    explicit EncodingGuard(CodeEditArea* area, QObject* parent = nullptr);
    ~EncodingGuard() override;

    /// Format of the document (from decode()); used by encodeForSave().
    void setFormat(const FileFormat& format);
    const FileFormat& format() const { return m_format; }

    /// Changes only the encoding, e.g. to "utf8" on the user's request.
    void setEncoding(const QString& encoding);
    QString encoding() const { return m_format.encoding; }

    using ChoiceFn = std::function<Choice(const QList<char32_t>& characters,
                                          const QString& encoding)>;
    /// Replaces the default message box (e.g. for tests or a custom UI).
    void setChoiceHandler(ChoiceFn fn) { m_choice = std::move(fn); }

    /// The default question: a message box over `parent`.
    static Choice askUser(QWidget* parent, const QList<char32_t>& characters,
                          const QString& encoding);

    /// Encodes the editor text for saving. Characters that slipped in
    /// without the guard (e.g. a replace done through the document) bring up
    /// the same choice. Returns false when the user cancels.
    bool encodeForSave(const QString& text, QByteArray* bytes);

signals:
    void encodingChanged(const QString& encoding);

private:
    bool filter(QString& text);
    Choice choose(const QList<char32_t>& characters);

    QPointer<CodeEditArea> m_area;
    FileFormat m_format;
    ChoiceFn m_choice;
};

} // namespace qce::encoding
