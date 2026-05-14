#include "WaterfallWidget.h"
#include "ModemBandwidth.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

// ── PulseAudio / PipeWire helpers ────────────────────────────────────────────

struct SrcEntry {
    int     index;
    QString name;        // PulseAudio source name (e.g. "alsa_input.xxx")
    QString description; // human-readable label
    bool    running;     // source is actively streaming
};

// Fallback: parse `pactl list sources short` (works on PipeWire & PA)
static QVector<SrcEntry> listSourcesPactl()
{
    QVector<SrcEntry> result;
    FILE* fp = popen("pactl list sources short 2>/dev/null", "r");
    if (!fp) return result;

    char line[512];
    int idx = 0;
    while (fgets(line, sizeof(line), fp)) {
        // Format: <idx>\t<name>\t<driver>\t<sample_spec>\t<state>\n
        QStringList parts = QString::fromUtf8(line).trimmed().split('\t');
        if (parts.size() < 5) continue;

        const QString name  = parts[1];
        const QString state = parts[4];

        // Skip monitor sources (name contains ".monitor")
        if (name.contains(".monitor")) continue;

        result.append({idx++,
                       name,
                       name.section('.', 1).replace('-', ' '),  // human-ish
                       state == QLatin1String("RUNNING")});
    }
    pclose(fp);
    return result;
}

// Primary: use PulseAudio C API to list capture sources.
// Falls back to pactl parsing if PA context connection fails or returns empty.
static QVector<SrcEntry> listSources()
{
    // ── Try PA context API first ──
    QVector<SrcEntry> result;

    pa_mainloop* ml = pa_mainloop_new();
    if (ml) {
        bool done = false;
        pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "neodigi-devices");
        if (ctx) {
            pa_context_set_state_callback(ctx, [](pa_context* c, void* userdata) {
                *static_cast<bool*>(userdata) = true;
            }, &done);

            if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) >= 0) {
                int ret = 0;
                for (int i = 0; i < 200 && !done; ++i)
                    pa_mainloop_iterate(ml, 1, &ret);

                if (pa_context_get_state(ctx) == PA_CONTEXT_READY) {
                    done = false;
                    pa_context_set_state_callback(ctx, nullptr, nullptr);

                    struct CbCtx { QVector<SrcEntry>* list; };
                    CbCtx cbCtx{&result};

                    pa_operation* op = pa_context_get_source_info_list(ctx,
                        [](pa_context*, const pa_source_info* i, int eol, void* userdata) {
                            auto* c = static_cast<CbCtx*>(userdata);
                            if (eol) return;
                            if (i->monitor_of_sink == PA_INVALID_INDEX) {
                                c->list->append({(int)c->list->size(),
                                                 QString::fromUtf8(i->name),
                                                 QString::fromUtf8(i->description),
                                                 i->state == PA_SOURCE_RUNNING});
                            }
                        }, &cbCtx);

                    if (op) {
                        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
                            pa_mainloop_iterate(ml, 1, &ret);
                        pa_operation_unref(op);
                    }
                }
                pa_context_disconnect(ctx);
            }
            pa_context_unref(ctx);
        }
        pa_mainloop_free(ml);
    }

    // If PA API returned results, use them
    if (!result.isEmpty())
        return result;

    // ── Fallback: pactl (works on PipeWire where PA context API may fail) ──
    qInfo("[Waterfall] PA source API returned empty — falling back to pactl");
    return listSourcesPactl();
}

// Find the first actively-streaming input source (fldigi's likely device)
static QString findRunningSource()
{
    const auto sources = listSources();
    for (const auto& s : sources) {
        if (s.running)
            return s.name;
    }
    return {};
}

// ── Color map ───────────────────────────────────────────────────────────────

QRgb WaterfallWidget::powerToColor(float norm)
{
    if (norm < 0.f) norm = 0.f;
    if (norm > 1.f) norm = 1.f;

    struct Stop { float pos; int r, g, b; };
    static const Stop stops[] = {
        {0.00f,   0,   0,   0},
        {0.20f,   0,   0, 139},
        {0.45f,   0, 255, 255},
        {0.65f,   0, 200,   0},
        {0.82f, 255, 255,   0},
        {1.00f, 255, 255, 255},
    };
    static const int N = sizeof(stops) / sizeof(stops[0]);

    for (int i = 1; i < N; ++i) {
        if (norm <= stops[i].pos) {
            float t = (norm - stops[i-1].pos) / (stops[i].pos - stops[i-1].pos);
            int r = (int)(stops[i-1].r + t * (stops[i].r - stops[i-1].r));
            int g = (int)(stops[i-1].g + t * (stops[i].g - stops[i-1].g));
            int b = (int)(stops[i-1].b + t * (stops[i].b - stops[i-1].b));
            return qRgb(r, g, b);
        }
    }
    return qRgb(255, 255, 255);
}

// ── Ctor / Dtor ─────────────────────────────────────────────────────────────

WaterfallWidget::WaterfallWidget(QWidget* parent)
    : QWidget(parent)
    , m_stubMode(true)
    , m_txActive(false)
    , m_carrierHz(1500)
    , m_modemBwHz(62)
    , m_deviceIdx(-1)
    , m_timer(new QTimer(this))
    , m_wfRow(0)
    , m_paSimple(nullptr)
    , m_fftwIn(nullptr)
    , m_fftwOut(nullptr)
    , m_fftwPlan(nullptr)
{
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    std::memset(m_audioBuffer, 0, sizeof(m_audioBuffer));
    connect(m_timer, &QTimer::timeout, this, &WaterfallWidget::onTimer);
    m_timer->start(1000 / TARGET_FPS);
}

WaterfallWidget::~WaterfallWidget()
{
    closeAudio();
}

// ── Public API ──────────────────────────────────────────────────────────────

void WaterfallWidget::setCarrierHz(int hz) { m_carrierHz = hz; }
void WaterfallWidget::setModemBandwidthHz(int hz) { m_modemBwHz = hz; }

void WaterfallWidget::setModemName(const QString& name)
{
    for (auto it = MODEM_BW.cbegin(); it != MODEM_BW.cend(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
            m_modemBwHz = it.value();
            return;
        }
    }
    m_modemBwHz = 100;
}

void WaterfallWidget::setTxActive(bool active)
{
    m_txActive = active;
}

void WaterfallWidget::setStubMode(bool enabled)
{
    m_stubMode = enabled;
    if (!m_stubMode) {
        if (!initAudio(nullptr)) m_stubMode = true;
    } else {
        closeAudio();
    }
}

bool WaterfallWidget::setAudioDevice(int deviceIdx)
{
    closeAudio();
    m_deviceIdx = deviceIdx;

    // deviceIdx ≤ 0: auto-detect / PA default (RUNNING sources first)
    if (deviceIdx <= 0)
        return initAudio(nullptr);

    // deviceIdx ≥ 1: look up real source (subtract 1 to skip "Default" in inputDevices())
    const auto sources = listSources();
    for (const auto& s : sources) {
        if (s.index == deviceIdx - 1) {
            m_stubMode = !initAudio(s.name.toUtf8().constData());
            return !m_stubMode;
        }
    }
    // Fall back to default
    m_stubMode = !initAudio(nullptr);
    return !m_stubMode;
}

bool WaterfallWidget::startAudioSource(const QString& paSourceName)
{
    // Build candidate list — specific source first, then running, then all, then PA default.
    // Intentionally does NOT close the existing connection first: if the new one fails
    // we keep the old one alive so the waterfall doesn't go dark.
    QStringList candidates;
    if (!paSourceName.isEmpty())
        candidates << paSourceName;
    const auto sources = listSources();
    for (const auto& s : sources) { if (s.running)  candidates << s.name; }
    for (const auto& s : sources) { if (!s.running) candidates << s.name; }
    candidates << QString();  // PA default (last resort)

    pa_sample_spec ss;
    ss.format   = PA_SAMPLE_FLOAT32LE;
    ss.rate     = SAMPLE_RATE;
    ss.channels = 1;

    pa_simple* newPa = nullptr;
    for (const QString& cand : candidates) {
        QByteArray nameUtf8;
        const char* tryName = nullptr;
        if (!cand.isEmpty()) { nameUtf8 = cand.toUtf8(); tryName = nameUtf8.constData(); }
        int error;
        newPa = pa_simple_new(nullptr, "neodigi", PA_STREAM_RECORD,
                              tryName, "waterfall", &ss, nullptr, nullptr, &error);
        if (newPa) {
            qInfo("[Waterfall] Capture started — source: %s", tryName ? tryName : "PA default");
            break;
        }
        qWarning("[Waterfall] pa_simple_new(%s): %s",
                 tryName ? tryName : "default", pa_strerror(error));
    }

    if (!newPa) {
        // Failed — keep existing connection if we have one
        qWarning("[Waterfall] All audio sources failed — %s",
                 m_paSimple ? "keeping existing connection" : "entering stub mode");
        m_stubMode = (m_paSimple == nullptr);
        return !m_stubMode;
    }

    // New connection succeeded — swap out old PA stream (keep FFTW)
    if (m_paSimple)
        pa_simple_free(m_paSimple);
    m_paSimple = newPa;

    // Allocate FFTW if not already present
    if (!m_fftwIn)   m_fftwIn   = fftw_alloc_real(FFT_SIZE);
    if (!m_fftwOut)  m_fftwOut  = fftw_alloc_complex(FFT_SIZE / 2 + 1);
    if (!m_fftwPlan) m_fftwPlan = fftw_plan_dft_r2c_1d(FFT_SIZE, m_fftwIn, m_fftwOut, FFTW_ESTIMATE);

    if (!m_fftwIn || !m_fftwOut || !m_fftwPlan) {
        qWarning("[Waterfall] FFTW alloc failed");
        pa_simple_free(m_paSimple);
        m_paSimple = nullptr;
        m_stubMode = true;
        return false;
    }

    m_stubMode = false;
    return true;
}


QVector<QPair<QString, QString>> WaterfallWidget::inputDevices()
{
    QVector<QPair<QString, QString>> result;
    result.append({QString(), "Default"});
    const auto sources = listSources();
    for (const auto& s : sources)
        result.append({s.name, s.description});
    return result;
}

// ── Audio init / close ──────────────────────────────────────────────────────

bool WaterfallWidget::initAudio(const char* sourceName)
{
    // ── Build a list of source names to try ─────────────────────────────
    QStringList candidates;

    if (sourceName) {
        // Specific source requested — try it, period.
        candidates << QString::fromUtf8(sourceName);
    } else {
        // Auto-detect: try actively-RUNNING sources first, then fall back
        // to the PA/PW system default (nullptr).
        // NOTA BENE: pa_simple_new(nullptr) activates the default source
        // even when SUSPENDED, returning a "successful" silent stream.
        // We MUST try RUNNING sources first to get real audio.
        const auto sources = listSources();
        for (const auto& s : sources) {
            if (s.running)
                candidates << s.name;
        }
        for (const auto& s : sources) {
            if (!s.running)
                candidates << s.name;
        }
        // PA default (may be SUSPENDED — last resort)
        candidates << QString();
    }

    // ── Try each candidate ──────────────────────────────────────────────
    pa_sample_spec ss;
    ss.format   = PA_SAMPLE_FLOAT32LE;
    ss.rate     = SAMPLE_RATE;
    ss.channels = 1;

    for (const QString& cand : candidates) {
        QByteArray    nameUtf8;
        const char*   tryName = nullptr;
        if (!cand.isEmpty()) {
            nameUtf8 = cand.toUtf8();
            tryName  = nameUtf8.constData();
        }

        int error;
        pa_simple* ps = pa_simple_new(nullptr, "neodigi", PA_STREAM_RECORD,
                                       tryName, "waterfall", &ss, nullptr, nullptr, &error);
        if (ps) {
            m_paSimple = ps;
            qInfo("[Waterfall] Capture started — source: %s",
                  tryName ? tryName : "PA default");
            break;
        }

        qWarning("[Waterfall] pa_simple_new(%s): %s",
                 tryName ? tryName : "default", pa_strerror(error));
    }

    if (!m_paSimple) {
        if (m_fftwPlan) { fftw_destroy_plan(m_fftwPlan); m_fftwPlan = nullptr; }
        if (m_fftwIn)   { fftw_free(m_fftwIn);  m_fftwIn  = nullptr; }
        if (m_fftwOut)  { fftw_free(m_fftwOut); m_fftwOut = nullptr; }
        return false;
    }

    // Allocate FFTW (after PA success — avoids leak on PA failure)
    if (!m_fftwIn)   m_fftwIn   = fftw_alloc_real(FFT_SIZE);
    if (!m_fftwOut)  m_fftwOut  = fftw_alloc_complex(FFT_SIZE / 2 + 1);
    if (!m_fftwPlan) m_fftwPlan = fftw_plan_dft_r2c_1d(FFT_SIZE, m_fftwIn, m_fftwOut, FFTW_ESTIMATE);

    if (!m_fftwIn || !m_fftwOut || !m_fftwPlan) {
        qWarning("[Waterfall] FFTW alloc failed");
        closeAudio();
        return false;
    }

    return true;
}

void WaterfallWidget::closeAudio()
{
    if (m_paSimple) {
        pa_simple_free(m_paSimple);
        m_paSimple = nullptr;
    }
    if (m_fftwPlan) { fftw_destroy_plan(m_fftwPlan); m_fftwPlan = nullptr; }
    if (m_fftwIn)   { fftw_free(m_fftwIn);  m_fftwIn  = nullptr; }
    if (m_fftwOut)  { fftw_free(m_fftwOut); m_fftwOut = nullptr; }
}

// ── Rendering loop ──────────────────────────────────────────────────────────

void WaterfallWidget::onTimer()
{
    if (m_stubMode)
        renderStubFrame();
    else
        renderLiveFrame();
    update();
}

void WaterfallWidget::renderStubFrame()
{
    if (m_wfImage.isNull()) return;
    static std::mt19937 rng(42);
    static std::uniform_real_distribution<float> noise(0.f, 0.12f);

    const int w = m_wfImage.width();
    const int rulerH = 16;
    const int wfH = height() - rulerH;
    if (wfH <= 0) return;

    if (m_wfRow >= wfH) m_wfRow = 0;

    for (int x = 0; x < w; ++x) {
        float freq = (float)x / w * 4000.f;
        float signal = noise(rng);
        float df = freq - m_carrierHz;
        signal += 0.6f * std::exp(-df * df / 800.f);
        signal += 0.2f * std::exp(-((freq - 800.f) * (freq - 800.f)) / 5000.f);
        signal += 0.15f * std::exp(-((freq - 2300.f) * (freq - 2300.f)) / 3000.f);
        m_wfImage.setPixel(x, m_wfRow, powerToColor(signal));
    }
    m_wfRow = (m_wfRow + 1) % wfH;
}

void WaterfallWidget::renderLiveFrame()
{
    if (!m_paSimple || !m_fftwPlan) return;

    // Read 256 samples (~32ms at 8000 Hz) from PulseAudio per tick
    constexpr int CHUNK = 256;
    float buf[CHUNK];
    int error;

    if (pa_simple_read(m_paSimple, buf, sizeof(buf), &error) < 0) {
        qWarning("[Waterfall] pa_simple_read: %s", pa_strerror(error));
        return;
    }

    // Emit audio level (RMS of this chunk → 0-100%)
    {
        float sumSq = 0.0f;
        for (int i = 0; i < CHUNK; ++i) sumSq += buf[i] * buf[i];
        float rms = std::sqrt(sumSq / CHUNK);
        int pct = static_cast<int>(qMin(1.0f, rms * 4.0f) * 100.0f);
        emit audioLevelChanged(pct);
    }

    // Rolling buffer: shift left, append new samples at the end
    memmove(m_audioBuffer, m_audioBuffer + CHUNK,
            (FFT_SIZE - CHUNK) * sizeof(float));
    memcpy(m_audioBuffer + FFT_SIZE - CHUNK, buf, CHUNK * sizeof(float));

    // Hann window + FFT
    for (int i = 0; i < FFT_SIZE; ++i) {
        double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (FFT_SIZE - 1)));
        m_fftwIn[i] = m_audioBuffer[i] * w;
    }
    fftw_execute(m_fftwPlan);

    // Render waterfall line
    const int bins = FFT_SIZE / 2 + 1;
    const int ww = m_wfImage.width();
    const int rulerH = 16;
    const int wfH = height() - rulerH;
    if (wfH <= 0) return;

    for (int x = 0; x < ww; ++x) {
        float freq = (float)x / ww * 4000.f;
        int bin = (int)(freq / SAMPLE_RATE * FFT_SIZE);
        if (bin >= bins) bin = bins - 1;
        double re = m_fftwOut[bin][0];
        double im = m_fftwOut[bin][1];
        double mag = std::sqrt(re * re + im * im) / FFT_SIZE;
        float norm = (float)(20.0 * std::log10(mag + 1e-10) / 60.0 + 1.0);
        m_wfImage.setPixel(x, m_wfRow, powerToColor(norm));
    }
    m_wfRow = (m_wfRow + 1) % wfH;
}

// ── Paint ───────────────────────────────────────────────────────────────────

void WaterfallWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_wfImage = QImage(width(), height(), QImage::Format_RGB32);
    m_wfImage.fill(Qt::black);
    m_wfRow = 0;
}

void WaterfallWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const int rulerH = 16;
    const int wfH = height() - rulerH;

    if (!m_wfImage.isNull() && wfH > 0) {
        QRect src(0, m_wfRow, m_wfImage.width(), wfH - m_wfRow);
        QRect dst(0, rulerH, width(), wfH - m_wfRow);
        painter.drawImage(dst, m_wfImage, src);

        if (m_wfRow > 0) {
            QRect src2(0, 0, m_wfImage.width(), m_wfRow);
            QRect dst2(0, rulerH + wfH - m_wfRow, width(), m_wfRow);
            painter.drawImage(dst2, m_wfImage, src2);
        }
    }

    drawFreqRuler(painter);
    drawCursor(painter);

    // TX active overlay
    if (m_txActive) {
        const int rulerH = 16;
        painter.fillRect(0, rulerH, width(), height() - rulerH,
                         QColor(0xf4, 0x47, 0x47, 25));
        painter.fillRect(width() / 2 - 90, height() / 2 - 12, 180, 24,
                         QColor(0x1e, 0x1e, 0x1e, 210));
        painter.setPen(QColor(0xf4, 0x47, 0x47));
        painter.setFont(QFont("monospace", 9, QFont::Bold));
        painter.drawText(0, height() / 2 - 12, width(), 24,
                         Qt::AlignCenter, "● TRANSMITTING");
    }

    // Overlay when no live audio source
    if (m_stubMode) {
        painter.fillRect(width() / 2 - 80, height() - 20, 160, 18,
                         QColor(0x1e, 0x1e, 0x1e, 200));
        painter.setPen(QColor(0xf4, 0x47, 0x47, 180));
        painter.setFont(QFont("monospace", 8));
        painter.drawText(0, height() - 18, width(), 16,
                         Qt::AlignCenter, "NO AUDIO INPUT");
    }
}

void WaterfallWidget::drawFreqRuler(QPainter& painter) const
{
    painter.fillRect(0, 0, width(), 16, QColor(0x18, 0x18, 0x18));
    painter.setPen(QColor(0x6a, 0x6a, 0x6a));
    painter.setFont(QFont("monospace", 7));

    static const int marks[] = {500, 750, 1000, 1250, 1500, 1750, 2000, 2500, 3000, 3500};
    for (int f : marks) {
        int x = (int)((float)f / 4000.f * width());
        painter.drawLine(x, 10, x, 16);
        painter.drawText(x - 12, 0, 26, 11, Qt::AlignCenter, QString::number(f));
    }
}

void WaterfallWidget::drawCursor(QPainter& painter) const
{
    const int x = (int)((float)m_carrierHz / 4000.f * width());
    const int bwPx = (int)((float)m_modemBwHz / 4000.f * width());
    const int rulerH = 16;

    // Bandwidth fill
    painter.fillRect(x - bwPx / 2, rulerH, bwPx, height() - rulerH,
                     QColor(0x4e, 0xc9, 0xb0, 40));
    // Left and right bandwidth edges
    painter.setPen(QPen(QColor(0x4e, 0xc9, 0xb0, 120), 1));
    painter.drawLine(x - bwPx / 2, rulerH, x - bwPx / 2, height());
    painter.drawLine(x + bwPx / 2, rulerH, x + bwPx / 2, height());
    // Center carrier cursor
    painter.setPen(QPen(QColor(0x4e, 0xc9, 0xb0), 1));
    painter.drawLine(x, rulerH, x, height());
}

void WaterfallWidget::mousePressEvent(QMouseEvent* event)
{
    int hz = (int)((float)event->pos().x() / width() * 4000.f);
    m_carrierHz = hz;
    emit carrierChanged(hz);
}
