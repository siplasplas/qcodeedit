#include <qce/kate/KateSyntaxVersion.h>

namespace qce::kate {

QString SyntaxVersion::toString() const {
    return QString::number(major) + QLatin1Char('.') + QString::number(minor);
}

QString SyntaxVersion::updateFileName() const {
    return QStringLiteral("update-") + toString() + QStringLiteral(".xml");
}

QString SyntaxVersion::updateUrl() const {
    return QStringLiteral("https://kate-editor.org/syntax/") + updateFileName();
}

std::optional<SyntaxVersion> SyntaxVersion::parse(QStringView text) {
    text = text.trimmed();
    const qsizetype dot = text.indexOf(QLatin1Char('.'));
    const QStringView majorPart = dot < 0 ? text : text.left(dot);
    const QStringView minorPart = dot < 0 ? QStringView() : text.mid(dot + 1);

    // QStringView::toInt accepts signs and surrounding spaces; require digits only.
    auto isDigits = [](QStringView s) {
        if (s.isEmpty()) return false;
        for (QChar c : s)
            if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
        return true;
    };
    if (!isDigits(majorPart)) return std::nullopt;
    if (dot >= 0 && !isDigits(minorPart)) return std::nullopt;

    bool ok = false;
    SyntaxVersion v;
    v.major = majorPart.toInt(&ok);
    if (!ok) return std::nullopt;
    if (dot >= 0) {
        v.minor = minorPart.toInt(&ok);
        if (!ok) return std::nullopt;
    }
    return v;
}

} // namespace qce::kate
