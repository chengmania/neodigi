#include "FreqDisplay.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QLineEdit>
#include <QTimer>
#include <QPolygonF>

// Segment bitmask: bit0=a(top) b(tr) c(br) d(bot) e(bl) f(tl) g(mid)
static constexpr uint8_t SEGS[10] = {
    0b0111111, // 0
    0b0000110, // 1
    0b1011011, // 2
    0b1001111, // 3
    0b1100110, // 4
    0b1101101, // 5
    0b1111101, // 6
    0b0000111, // 7
    0b1111111, // 8
    0b1101111, // 9
};

// Hexagonal horizontal segment polygon
static QPolygonF hSeg(qreal x, qreal y, qreal w, qreal h)
{
    qreal b = h * 0.55;
    return QPolygonF({
        {x + b,     y},
        {x + w - b, y},
        {x + w,     y + h / 2.0},
        {x + w - b, y + h},
        {x + b,     y + h},
        {x,         y + h / 2.0}
    });
}

// Hexagonal vertical segment polygon
static QPolygonF vSeg(qreal x, qreal y, qreal w, qreal h)
{
    qreal b = w * 0.55;
    return QPolygonF({
        {x + w / 2.0, y},
        {x + w,       y + b},
        {x + w,       y + h - b},
        {x + w / 2.0, y + h},
        {x,           y + h - b},
        {x,           y + b}
    });
}

// ---------------------------------------------------------------------------

FreqDisplay::FreqDisplay(QWidget* parent)
    : QWidget(parent)
    , m_hz(14070000.0)
    , m_editor(new QLineEdit(this))
    , m_blinkTimer(new QTimer(this))
    , m_cursorOn(true)
{
    setObjectName("FreqDisplay");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFocusPolicy(Qt::ClickFocus);
    setCursor(Qt::IBeamCursor);
    setToolTip("Click to enter frequency  |  Scroll to tune\n"
               "Ctrl+Scroll = ×100 Hz  |  Shift+Scroll = ×1 kHz");

    // Overlay editor — shown only in edit mode
    m_editor->setObjectName("FreqEditor");
    m_editor->setAlignment(Qt::AlignCenter);
    m_editor->setFrame(false);
    m_editor->setInputMethodHints(Qt::ImhDigitsOnly);
    m_editor->hide();
    m_editor->setPlaceholderText("Hz (e.g. 14070000)");
    connect(m_editor, &QLineEdit::returnPressed, this, &FreqDisplay::commitEdit);
    connect(m_editor, &QLineEdit::editingFinished, this, &FreqDisplay::commitEdit);

    connect(m_blinkTimer, &QTimer::timeout, this, &FreqDisplay::onCursorBlink);
}

// ---------------------------------------------------------------------------
// Size
// ---------------------------------------------------------------------------

QSize FreqDisplay::sizeHint() const
{
    // Fixed format: up to 3 mhz digits + dot + 3 + dot + 3 = max 9 cells + 2 dots
    return {HPAD + 9 * CW + 2 * DW + HPAD, VPAD + CH + VPAD};
}

QSize FreqDisplay::minimumSizeHint() const { return sizeHint(); }

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void FreqDisplay::drawSegment(QPainter& p, bool on, const QPolygonF& poly) const
{
    static const QColor LIT(0x9c, 0xdc, 0xfe);          // cyan accent
    static const QColor DIM(0x0e, 0x1d, 0x28);          // near-black tint
    p.setBrush(on ? LIT : DIM);
    p.setPen(Qt::NoPen);
    p.drawPolygon(poly);
}

void FreqDisplay::drawDigit(QPainter& p, int x, int y, int d) const
{
    const uint8_t on = (d >= 0 && d <= 9) ? SEGS[d] : 0;
    const qreal W = CW, H = CH, T = ST, G = SG;
    const qreal mid = H / 2.0;
    const qreal fx = x, fy = y;

    // a — top horizontal
    drawSegment(p, on & 0x01, hSeg(fx + G,     fy,             W - 2*G, T));
    // b — top-right vertical
    drawSegment(p, on & 0x02, vSeg(fx + W - T, fy + G,         T, mid - 2*G));
    // c — bottom-right vertical
    drawSegment(p, on & 0x04, vSeg(fx + W - T, fy + mid + G,   T, mid - 2*G));
    // d — bottom horizontal
    drawSegment(p, on & 0x08, hSeg(fx + G,     fy + H - T,     W - 2*G, T));
    // e — bottom-left vertical
    drawSegment(p, on & 0x10, vSeg(fx,          fy + mid + G,   T, mid - 2*G));
    // f — top-left vertical
    drawSegment(p, on & 0x20, vSeg(fx,          fy + G,         T, mid - 2*G));
    // g — middle horizontal
    drawSegment(p, on & 0x40, hSeg(fx + G,     fy + mid - T/2.0, W - 2*G, T));
}

void FreqDisplay::drawDot(QPainter& p, int x, int y) const
{
    static const QColor LIT(0x9c, 0xdc, 0xfe);
    const qreal sz = ST + 1;
    p.setBrush(LIT);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(x + (DW - sz) / 2.0, y + CH - sz - 1, sz, sz));
}

void FreqDisplay::paintEvent(QPaintEvent*)
{
    if (m_editor->isVisible()) return;   // editor covers us

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x18, 0x18, 0x18));

    // Frequency breakdown
    auto hz = (long long)m_hz;
    long long mhz = hz / 1'000'000;
    long long khz = (hz % 1'000'000) / 1'000;
    long long sub = hz % 1'000;

    QString mhzStr = QString::number(mhz);
    QString khzStr = QString::number(khz).rightJustified(3, '0');
    QString subStr = QString::number(sub).rightJustified(3, '0');

    // Compute total pixel width to center within sizeHint
    int nDigits = mhzStr.length() + 3 + 3;
    int totalW   = nDigits * CW + 2 * DW;
    int startX   = (width() - totalW) / 2;
    int startY   = VPAD;

    int x = startX;

    for (QChar c : mhzStr) { drawDigit(p, x, startY, c.toLatin1() - '0'); x += CW; }
    drawDot(p, x, startY); x += DW;
    for (QChar c : khzStr) { drawDigit(p, x, startY, c.toLatin1() - '0'); x += CW; }
    drawDot(p, x, startY); x += DW;
    for (QChar c : subStr) { drawDigit(p, x, startY, c.toLatin1() - '0'); x += CW; }

    // Glow border when focused
    if (hasFocus() && !m_editor->isVisible()) {
        p.setPen(QPen(QColor(0x9c, 0xdc, 0xfe, 60), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void FreqDisplay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;

    m_editor->setText(QString::number((long long)m_hz));
    m_editor->setGeometry(rect());
    m_editor->show();
    m_editor->setFocus();
    m_editor->selectAll();
    m_blinkTimer->stop();
}

void FreqDisplay::wheelEvent(QWheelEvent* event)
{
    int raw = event->angleDelta().y();
    if (raw == 0) { event->ignore(); return; }

    double step = 1.0;
    if (event->modifiers() & Qt::ControlModifier) step = 100.0;
    else if (event->modifiers() & Qt::ShiftModifier) step = 1000.0;

    m_hz += (raw > 0 ? step : -step);
    if (m_hz < 0) m_hz = 0;

    emit frequencyChanged(m_hz);
    update();
    event->accept();
}

void FreqDisplay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_editor->isVisible())
        m_editor->setGeometry(rect());
}

void FreqDisplay::commitEdit()
{
    if (!m_editor->isVisible()) return;

    QString raw = m_editor->text()
                      .remove('.')
                      .remove(',')
                      .remove(' ');
    bool ok = false;
    double hz = raw.toDouble(&ok);
    if (ok && hz > 0.0) {
        m_hz = hz;
        emit frequencyChanged(m_hz);
    }

    m_editor->hide();
    update();
}

void FreqDisplay::setFrequencyHz(double hz)
{
    if (hz < 0) hz = 0;
    m_hz = hz;
    update();
}

void FreqDisplay::onCursorBlink()
{
    m_cursorOn = !m_cursorOn;
    update();
}
