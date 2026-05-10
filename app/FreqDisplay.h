#pragma once

#include <QWidget>

class QLineEdit;
class QTimer;

class FreqDisplay : public QWidget {
    Q_OBJECT

public:
    explicit FreqDisplay(QWidget* parent = nullptr);

    void   setFrequencyHz(double hz);
    double frequencyHz() const { return m_hz; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void frequencyChanged(double hz);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private slots:
    void commitEdit();
    void onCursorBlink();

private:
    // Layout constants (px)
    static constexpr int CW   = 22;  // digit cell width
    static constexpr int CH   = 42;  // digit cell height
    static constexpr int DW   = 9;   // dot slot width
    static constexpr int HPAD = 10;  // horizontal padding
    static constexpr int VPAD = 7;   // vertical padding
    static constexpr int ST   = 4;   // segment thickness
    static constexpr int SG   = 2;   // segment gap

    void drawDigit(QPainter& p, int x, int y, int d) const;
    void drawDot(QPainter& p, int x, int y) const;
    void drawSegment(QPainter& p, bool on, const QPolygonF& poly) const;

    double     m_hz;
    QLineEdit* m_editor;
    QTimer*    m_blinkTimer;
    bool       m_cursorOn;
};
