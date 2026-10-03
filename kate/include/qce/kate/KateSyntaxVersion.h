#pragma once

#include <QString>

#include <optional>

namespace qce::kate {

/// Version of the KDE syntax-highlighting data set (e.g. 6.31), as published
/// at https://kate-editor.org/syntax/update-<major>.<minor>.xml.
///
/// Kept as two integers rather than a string so versions compare and iterate
/// naturally (6.9 < 6.31).
struct SyntaxVersion {
    int major = 0;
    int minor = 0;

    /// "6.31"
    QString toString() const;

    /// "update-6.31.xml"
    QString updateFileName() const;

    /// "https://kate-editor.org/syntax/update-6.31.xml"
    QString updateUrl() const;

    /// Parse "X.Y" (as found in a Kate XML `kateversion` attribute).
    /// A bare "X" is accepted as X.0. Returns nullopt on malformed input.
    static std::optional<SyntaxVersion> parse(QStringView text);

    // Spelled out (no <=>) so the header stays usable from C++17 consumers.
    friend constexpr bool operator==(SyntaxVersion a, SyntaxVersion b) {
        return a.major == b.major && a.minor == b.minor;
    }
    friend constexpr bool operator!=(SyntaxVersion a, SyntaxVersion b) { return !(a == b); }
    friend constexpr bool operator<(SyntaxVersion a, SyntaxVersion b) {
        return a.major != b.major ? a.major < b.major : a.minor < b.minor;
    }
    friend constexpr bool operator>(SyntaxVersion a, SyntaxVersion b)  { return b < a; }
    friend constexpr bool operator<=(SyntaxVersion a, SyntaxVersion b) { return !(b < a); }
    friend constexpr bool operator>=(SyntaxVersion a, SyntaxVersion b) { return !(a < b); }
};

/// The data-set version KateXmlReader has been validated against.
/// Bump by hand after checking the parser on a newer set
/// (tools/scan_unknown_attrs.py + Kate regression tests).
constexpr SyntaxVersion supportedSyntaxVersion() { return {6, 31}; }

} // namespace qce::kate
