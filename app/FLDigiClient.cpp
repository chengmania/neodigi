#include "FLDigiClient.h"

#include <QNetworkRequest>
#include <QEventLoop>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>

// Backoff config
static constexpr int BACKOFF_INIT_MS = 2000;
static constexpr int BACKOFF_MAX_MS  = 30000;

FLDigiClient::FLDigiClient(QObject* parent)
    : QObject(parent)
    , m_stubMode(false)
    , m_connected(false)
    , m_callInProgress(false)
    , m_callTimeout(2000)
    , m_backoffMs(BACKOFF_INIT_MS)
    , m_endpoint(QString("http://%1:%2/RPC2").arg(XMLRPC_HOST).arg(XMLRPC_PORT))
    , m_nam(new QNetworkAccessManager(this))
    , m_backoffTimer(new QTimer(this))
{
    m_backoffTimer->setSingleShot(true);
    connect(m_backoffTimer, &QTimer::timeout, this, &FLDigiClient::attemptReconnect);
    // Kick off the first connection attempt as soon as the event loop starts.
    // Without this, the poll timer silently returns every cycle (not connected)
    // and the backoff timer never starts (it only starts after a failed call).
    QTimer::singleShot(0, this, &FLDigiClient::attemptReconnect);
}

FLDigiClient::~FLDigiClient() = default;

void FLDigiClient::setStubMode(bool enabled)
{
    m_stubMode = enabled;
    if (enabled) {
        m_backoffTimer->stop();
        if (!m_connected) {
            m_connected = true;
            emit connectionStateChanged(true);
        }
    }
}

void FLDigiClient::setEndpoint(const QString& host, int port)
{
    m_endpoint = QString("http://%1:%2/RPC2").arg(host).arg(port);
    // Reset connection state so backoff restarts against new endpoint
    m_connected  = false;
    m_backoffMs  = BACKOFF_INIT_MS;
    m_backoffTimer->stop();
}

// ---------------------------------------------------------------------------
// Connection state helpers
// ---------------------------------------------------------------------------

void FLDigiClient::onCallSuccess()
{
    const bool wasDown = !m_connected;
    m_connected = true;
    m_backoffMs = BACKOFF_INIT_MS;
    m_backoffTimer->stop();
    if (wasDown) {
        qInfo("[XMLRPC] Connected to fldigi at %s", qPrintable(m_endpoint));
        emit connectionStateChanged(true);
    }
}

void FLDigiClient::onCallFailed(const QString& method, const QString& reason)
{
    qWarning("[XMLRPC] ✗ %s — %s", qPrintable(method), qPrintable(reason));

    const bool wasUp = m_connected;
    m_connected = false;

    if (!m_backoffTimer->isActive()) {
        qInfo("[XMLRPC] Scheduling reconnect in %d ms", m_backoffMs);
        m_backoffTimer->start(m_backoffMs);
        m_backoffMs = qMin(m_backoffMs * 2, BACKOFF_MAX_MS);
    }

    if (wasUp)
        emit connectionStateChanged(false);
}

void FLDigiClient::attemptReconnect()
{
    qInfo("[XMLRPC] Attempting reconnect to %s ...", qPrintable(m_endpoint));
    // Lightweight ping — if this succeeds, onCallSuccess() restores normal polling
    call("fldigi.name");
}

// ---------------------------------------------------------------------------
// XML-RPC wire layer
// ---------------------------------------------------------------------------

QByteArray FLDigiClient::buildCall(const QString& method, const QVariantList& params) const
{
    QByteArray body;
    QXmlStreamWriter xml(&body);
    xml.writeStartDocument();
    xml.writeStartElement("methodCall");
    xml.writeTextElement("methodName", method);
    xml.writeStartElement("params");
    for (const QVariant& v : params) {
        xml.writeStartElement("param");
        xml.writeStartElement("value");
        switch (v.typeId()) {
            case QMetaType::Bool:
                xml.writeTextElement("boolean", v.toBool() ? "1" : "0");
                break;
            case QMetaType::Int:
                xml.writeTextElement("int", QString::number(v.toInt()));
                break;
            case QMetaType::Double:
                xml.writeTextElement("double", QString::number(v.toDouble(), 'f', 6));
                break;
            default:
                xml.writeTextElement("string", v.toString());
                break;
        }
        xml.writeEndElement(); // value
        xml.writeEndElement(); // param
    }
    xml.writeEndElement(); // params
    xml.writeEndElement(); // methodCall
    xml.writeEndDocument();
    return body;
}

QVariant FLDigiClient::parseValue(QXmlStreamReader& xml) const
{
    // On entry, xml is positioned at a <value> StartElement.
    // Read next token — it's either bare text (fldigi shorthand)
    // or a typed child element (<string>, <int>, <i4>, <double>, etc.).
    xml.readNext();

    // Bare text inside <value> — valid XML-RPC, defaults to string.
    // fldigi sends <value>BPSK31</value> without <string> wrapper.
    if (xml.tokenType() == QXmlStreamReader::Characters)
        return xml.text().toString();

    if (xml.tokenType() != QXmlStreamReader::StartElement)
        return {};

    const QString tag = xml.name().toString();

    if (tag == QLatin1String("string"))  return xml.readElementText();
    if (tag == QLatin1String("boolean")) return xml.readElementText() == QLatin1String("1");
    if (tag == QLatin1String("int") || tag == QLatin1String("i4"))
                                         return xml.readElementText().toInt();
    if (tag == QLatin1String("double"))  return xml.readElementText().toDouble();
    if (tag == QLatin1String("base64"))
        return QByteArray::fromBase64(xml.readElementText().toLatin1());

    if (tag == QLatin1String("array")) {
        // <array><data><value>…</value>…</data></array>
        QStringList items;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("value")) {
                QVariant v = parseValue(xml);
                if (v.isValid()) items << v.toString();
            } else if (xml.isEndElement() && xml.name() == QLatin1String("array")) {
                break;
            }
        }
        return items;
    }

    xml.skipCurrentElement();
    return {};
}

QVariant FLDigiClient::parseResponse(const QByteArray& data) const
{
    const bool isFault = data.contains("<fault>");

    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("value")) {
            QVariant result = parseValue(xml);
            if (isFault) {
                qWarning("[XMLRPC] fault: %s", qPrintable(result.toString()));
                return {};
            }
            return result;
        }
    }
    return {};
}

QVariant FLDigiClient::call(const QString& method, const QVariantList& params)
{
    if (m_stubMode)
        return {};

    // ── Re-entrancy guard ─────────────────────────────────────────────────
    // Prevents the 500ms poll timer from firing again while we're blocked
    // inside a nested QEventLoop, which would create cascading calls.
    if (m_callInProgress) {
        qDebug("[XMLRPC] ⚠ dropped (previous call still pending): %s", qPrintable(method));
        return {};
    }
    m_callInProgress = true;

    // ── Debug log outgoing call ───────────────────────────────────────────
    if (params.isEmpty()) {
        qDebug("[XMLRPC] → %s", qPrintable(method));
    } else {
        qDebug("[XMLRPC] → %s  args=%s",
               qPrintable(method),
               qPrintable(params.first().toString()));
    }

    // ── Build and send ────────────────────────────────────────────────────
    QNetworkRequest req{QUrl(m_endpoint)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml");
    // fldigi's XML-RPC server is HTTP/1.0 — one request per connection.
    // Without this header Qt sends HTTP/1.1 keep-alive, which causes fldigi
    // to SIGSEGV when it tries to read a second request on the same socket.
    req.setRawHeader("Connection", "close");
    req.setTransferTimeout(m_callTimeout);      // abort if no response in time

    const QByteArray body = buildCall(method, params);
    QNetworkReply* reply  = m_nam->post(req, body);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    // ── Handle result ─────────────────────────────────────────────────────
    QVariant result;

    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        // Raw response helps diagnose wrong method names / parameter types
        qDebug("[XMLRPC] raw(%s): %.*s",
               qPrintable(method), (int)qMin(data.size(), (qsizetype)400), data.constData());
        result = parseResponse(data);
        qDebug("[XMLRPC] ← %s = %s",
               qPrintable(method),
               qPrintable(result.toString().left(120)));
        onCallSuccess();
    } else {
        const QString reason = (reply->error() == QNetworkReply::OperationCanceledError)
                               ? QStringLiteral("timeout (%1 ms)").arg(m_callTimeout)
                               : reply->errorString();
        onCallFailed(method, reason);
    }

    reply->deleteLater();
    m_callInProgress = false;
    return result;
}

// ---------------------------------------------------------------------------
// Modem
// ---------------------------------------------------------------------------

QString FLDigiClient::getModemName()
{
    if (m_stubMode) return "BPSK31";
    return call("modem.get_name").toString();
}

void FLDigiClient::setModemByName(const QString& name)
{
    if (!m_stubMode) call("modem.set_by_name", {name});
}

QStringList FLDigiClient::getModemNames()
{
    if (m_stubMode) return {"BPSK31", "BPSK63", "BPSK125", "RTTY", "CW", "OLIVIA"};
    return call("modem.get_names").toStringList();
}

bool FLDigiClient::getAfc()
{
    if (m_stubMode) return true;
    return call("main.get_afc").toBool();
}

void FLDigiClient::setAfc(bool enabled)
{
    if (!m_stubMode) call("main.set_afc", {enabled});
}

bool FLDigiClient::getReverse()
{
    if (m_stubMode) return false;
    return call("main.get_reverse").toBool();
}

void FLDigiClient::setReverse(bool enabled)
{
    if (!m_stubMode) call("main.set_reverse", {enabled});
}

double FLDigiClient::getSquelch()
{
    if (m_stubMode) return 35.0;
    return call("main.get_squelch_level").toDouble();
}

void FLDigiClient::setSquelch(double level)
{
    if (!m_stubMode) call("main.set_squelch_level", {level});
}

bool FLDigiClient::getSquelchOnOff()
{
    if (m_stubMode) return true;
    return call("main.get_squelch").toBool();
}

void FLDigiClient::setSquelchOnOff(bool enabled)
{
    if (!m_stubMode) call("main.set_squelch", {enabled});
}

int FLDigiClient::getCarrier()
{
    if (m_stubMode) return 1500;
    return call("modem.get_carrier").toInt();
}

void FLDigiClient::setCarrier(int hz)
{
    if (!m_stubMode) call("modem.set_carrier", {hz});
}

double FLDigiClient::getQuality()
{
    if (m_stubMode) return 0.85;
    return call("modem.get_quality").toDouble();
}

// ---------------------------------------------------------------------------
// Rig
// ---------------------------------------------------------------------------

double FLDigiClient::getRigFrequency()
{
    if (m_stubMode) return 14070000.0;
    return call("rig.get_frequency").toDouble();
}

void FLDigiClient::setRigFrequency(double hz)
{
    if (!m_stubMode) call("rig.set_frequency", {hz});
}

// ---------------------------------------------------------------------------
// Text  (text.get_rx omitted — crashes fldigi, see CLAUDE.md §14)
// rx.get_data is safe — returns all RX data since last query, mutex-protected.
// ---------------------------------------------------------------------------

QString FLDigiClient::getRxData()
{
    if (m_stubMode) return {};
    QVariant result = call("rx.get_data");

    // rx.get_data returns base64 → decoded to QByteArray.
    // Bytes MUST be treated as Latin1, not UTF-8, to preserve 8-bit
    // characters and avoid mojibake from RSID / control bytes.
    if (result.typeId() == QMetaType::QByteArray) {
        const QByteArray raw = result.toByteArray();
        return QString::fromLatin1(raw);
    }
    return result.toString();
}

void FLDigiClient::clearRx()
{
    if (!m_stubMode) call("text.clear_rx");
}

void FLDigiClient::addTx(const QString& text)
{
    if (!m_stubMode) call("text.add_tx", {text});
}

void FLDigiClient::clearTx()
{
    if (!m_stubMode) call("text.clear_tx");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

QString FLDigiClient::getTrxState()
{
    if (m_stubMode) return "RX";
    return call("main.get_trx_state").toString();
}

void FLDigiClient::startTx()
{
    if (!m_stubMode) call("main.tx");
}

void FLDigiClient::startRx()
{
    if (!m_stubMode) call("main.rx");
}

void FLDigiClient::tune()
{
    if (!m_stubMode) call("main.tune");
}

bool FLDigiClient::getLock()
{
    if (m_stubMode) return false;
    return call("main.get_lock").toBool();
}

void FLDigiClient::setLock(bool enabled)
{
    if (!m_stubMode) call("main.set_lock", {enabled});
}

// ---------------------------------------------------------------------------
// IDs / spot  (fldigi uses main.get/set_txid, main.get/set_rsid, spot.get/set_auto)
// ---------------------------------------------------------------------------

bool FLDigiClient::getTxId()
{
    if (m_stubMode) return false;
    return call("main.get_txid").toBool();
}

void FLDigiClient::setTxId(bool enabled)
{
    if (!m_stubMode) call("main.set_txid", {enabled});
}

bool FLDigiClient::getRxId()
{
    if (m_stubMode) return false;
    return call("main.get_rsid").toBool();
}

void FLDigiClient::setRxId(bool enabled)
{
    if (!m_stubMode) call("main.set_rsid", {enabled});
}

bool FLDigiClient::getSpot()
{
    if (m_stubMode) return false;
    return call("spot.get_auto").toBool();
}

void FLDigiClient::setSpot(bool enabled)
{
    if (!m_stubMode) call("spot.set_auto", {enabled});
}
