#pragma once

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QVector>
#include <QPair>
#include <QString>

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/context.h>
#include <pulse/mainloop.h>
#include <pulse/introspect.h>
#include <fftw3.h>

class WaterfallWidget : public QWidget {
    Q_OBJECT

public:
    static constexpr int FFT_SIZE   = 4096;
    static constexpr int SAMPLE_RATE = 8000;
    static constexpr int TARGET_FPS  = 30;

    explicit WaterfallWidget(QWidget* parent = nullptr);
    ~WaterfallWidget() override;

    void setCarrierHz(int hz);
    void setModemBandwidthHz(int hz);
    void setModemName(const QString& name);
    void setTxActive(bool active);
    void setStubMode(bool enabled);
    bool isStubMode() const { return m_stubMode; }
    bool setAudioDevice(int deviceIdx);

    // Name-based audio start: empty name = auto-detect RUNNING source then PA default.
    // Always sets m_stubMode. Falls back gracefully — never crashes.
    bool startAudioSource(const QString& paSourceName);

    // Returns list of (pulse_source_name, human_description)
    static QVector<QPair<QString, QString>> inputDevices();

signals:
    void carrierChanged(int hz);
    void audioLevelChanged(int pct);   // 0-100, emitted each live render frame

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTimer();

private:
    bool initAudio(const char* sourceName);
    void closeAudio();

    void renderStubFrame();
    void renderLiveFrame();
    void drawFreqRuler(QPainter& painter) const;
    void drawCursor(QPainter& painter) const;

    static QRgb powerToColor(float normalized);

    bool    m_stubMode;
    bool    m_txActive;
    int     m_carrierHz;
    int     m_modemBwHz;
    int     m_deviceIdx;

    QTimer* m_timer;
    QImage  m_wfImage;
    int     m_wfRow;

    // PulseAudio
    pa_simple*   m_paSimple;
    float        m_audioBuffer[FFT_SIZE];

    // FFT
    double*       m_fftwIn;
    fftw_complex* m_fftwOut;
    fftw_plan     m_fftwPlan;
};
