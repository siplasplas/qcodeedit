#include <qce/encoding/EncodingGuard.h>

#include <qce/CodeEditArea.h>

#include <QMessageBox>
#include <QPushButton>

namespace qce::encoding {

namespace {

// "ą (U+0105), ż (U+017C)" for the first few characters.
QString describe(const QList<char32_t>& characters) {
    constexpr int kShown = 8;
    QStringList parts;
    for (int i = 0; i < characters.size() && i < kShown; ++i) {
        const char32_t cp = characters[i];
        parts << QStringLiteral("%1 (U+%2)")
                     .arg(QString::fromUcs4(&cp, 1))
                     .arg(uint(cp), 4, 16, QLatin1Char('0')).toUpper();
    }
    if (characters.size() > kShown) parts << QStringLiteral("…");
    return parts.join(QStringLiteral(", "));
}

} // namespace

struct EncodingGuard::Private {
    QPointer<CodeEditArea> m_area;
    FileFormat m_format;
    ChoiceFn m_choice;
};

EncodingGuard::EncodingGuard(CodeEditArea* area, QObject* parent)
    :
    QObject(parent),
      d(std::make_unique<Private>()) {
    d->m_area = area;
    if (d->m_area)
        d->m_area->setInsertFilter([this](QString& text) { return filter(text); });
}

EncodingGuard::~EncodingGuard() {
    if (d->m_area) d->m_area->setInsertFilter({});
}

void EncodingGuard::setFormat(const FileFormat& format) {
    const bool changed = format.encoding != d->m_format.encoding;
    d->m_format = format;
    if (changed) emit encodingChanged(d->m_format.encoding);
}

void EncodingGuard::setEncoding(const QString& encoding) {
    if (encoding == d->m_format.encoding) return;
    d->m_format.encoding = encoding;
    // A BOM belongs to the Unicode encodings only.
    if (!isUnicode(encoding)) d->m_format.bom = false;
    emit encodingChanged(encoding);
}

EncodingGuard::Choice EncodingGuard::askUser(QWidget* parent,
                                             const QList<char32_t>& characters,
                                             const QString& encoding) {
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QObject::tr("Characters outside %1").arg(encoding));
    box.setText(QObject::tr("The document is saved in %1, which cannot store: %2")
                    .arg(encoding, describe(characters)));
    box.setInformativeText(QObject::tr("Replace them with '?', or switch the document "
                                       "to UTF-8 (it will be saved as UTF-8)?"));
    auto* replace = box.addButton(QObject::tr("Replace with '?'"), QMessageBox::AcceptRole);
    auto* utf8 = box.addButton(QObject::tr("Switch to UTF-8"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(utf8);
    box.exec();
    if (box.clickedButton() == replace) return Choice::Replace;
    if (box.clickedButton() == utf8) return Choice::SwitchToUtf8;
    return Choice::Cancel;
}

EncodingGuard::Choice EncodingGuard::choose(const QList<char32_t>& characters) {
    if (d->m_choice) return d->m_choice(characters, d->m_format.encoding);
    return askUser(d->m_area ? d->m_area->window() : nullptr, characters, d->m_format.encoding);
}

bool EncodingGuard::filter(QString& text) {
    const QList<char32_t> bad = unrepresentable(text, d->m_format.encoding);
    if (bad.isEmpty()) return true;
    switch (choose(bad)) {
    case Choice::Replace:
        text = replaceUnrepresentable(text, d->m_format.encoding);
        return true;
    case Choice::SwitchToUtf8:
        setEncoding(QStringLiteral("utf8"));
        return true;
    case Choice::Cancel:
        break;
    }
    return false;
}

bool EncodingGuard::encodeForSave(const QString& text, QByteArray* bytes, bool exactText) {
    auto run = [&](bool replace) {
        return exactText ? encodeExact(text, d->m_format.encoding, d->m_format.bom, replace)
                         : encode(text, d->m_format, replace);
    };
    EncodeResult result = run(false);
    if (!result.ok && !result.unrepresentable.isEmpty()) {
        switch (choose(result.unrepresentable)) {
        case Choice::Replace:
            result = run(true);
            break;
        case Choice::SwitchToUtf8:
            setEncoding(QStringLiteral("utf8"));
            result = run(false);
            break;
        case Choice::Cancel:
            return false;
        }
    }
    if (!result.ok) return false;
    if (bytes) *bytes = std::move(result.bytes);
    return true;
}


const FileFormat& EncodingGuard::format() const { return d->m_format; }
QString EncodingGuard::encoding() const { return d->m_format.encoding; }
void EncodingGuard::setChoiceHandler(ChoiceFn fn) { d->m_choice = std::move(fn); }

} // namespace qce::encoding
