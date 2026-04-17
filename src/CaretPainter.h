#pragma once

#include <QFont>
#include <QObject>

class QTimer;
class QPainter;

namespace qce {

struct TextCursor;
struct ViewportState;

/// Internal helper that draws and blinks the text insertion caret.
///
/// Owns its own QTimer so CodeEditArea stays small. CodeEditArea connects
/// blinkToggled() to viewport()->update() and calls setFocused()/resetBlink()
/// at the right moments.
class CaretPainter : public QObject {
    Q_OBJECT
public:
    explicit CaretPainter(QObject* parent = nullptr);

    void setBlinkInterval(int ms);
    int blinkInterval() const { return m_blinkInterval; }

    /// Called when the host widget gains or loses keyboard focus.
    void setFocused(bool focused);

    /// Resets the blink phase so the caret is immediately visible after a
    /// cursor move. No-op if not focused.
    void resetBlink();

    void setOverwrite(bool overwrite);
    bool overwrite() const { return m_overwrite; }

    /// Paints the caret. No-op when hidden, out of focus, or off-screen.
    /// `visualCol` is the tab-expanded column used to compute the pixel x
    /// position; the caller (CodeEditArea) derives it via
    /// LineRenderer::visualColumn().
    /// visualCol  — tab-expanded column (from LineRenderer::visualColumn).
    /// visualRow  — absolute visual row index (equals cursor.line when !wordWrap).
    void paint(QPainter& painter,
               const TextCursor& cursor,
               int visualCol,
               int visualRow,
               const ViewportState& vp,
               const QFont& font) const;

signals:
    /// Emitted each time the blink phase changes or focus state changes.
    void blinkToggled();

private slots:
    void onTimerTick();

private:
    QTimer* m_timer;
    bool m_shown     = true;
    bool m_focused   = false;
    bool m_overwrite = false;
    int  m_blinkInterval = 500;
};

} // namespace qce
