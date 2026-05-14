#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QXmlStreamReader>

static const int XMLRPC_PORT = 7362;
static const char XMLRPC_HOST[] = "localhost";

// Minimum interval between consecutive poll cycles (ms)
static const int XMLRPC_POLL_MIN_MS = 500;

class FLDigiClient : public QObject {
    Q_OBJECT

public:
    explicit FLDigiClient(QObject* parent = nullptr);
    ~FLDigiClient() override;

    bool isStubMode()        const { return m_stubMode; }
    bool isConnected()       const { return m_connected; }
    bool isCallInProgress()  const { return m_callInProgress; }

    void setStubMode(bool enabled);
    void setEndpoint(const QString& host, int port);
    void setCallTimeout(int ms) { m_callTimeout = ms; }

    // Modem
    QString     getModemName();
    void        setModemByName(const QString& name);
    QStringList getModemNames();
    bool        getAfc();
    void        setAfc(bool enabled);
    bool        getReverse();
    void        setReverse(bool enabled);
    double      getSquelch();
    void        setSquelch(double level);
    bool        getSquelchOnOff();
    void        setSquelchOnOff(bool enabled);
    int         getCarrier();
    void        setCarrier(int hz);
    double      getQuality();

    // Rig
    double getRigFrequency();
    void   setRigFrequency(double hz);

    // Text  (text.get_rx REMOVED — crashes fldigi, see CLAUDE.md §14)
    // rx.get_data returns all RX text since last query — safe, mutex-protected.
    QString getRxData();
    void    clearRx();
    void    addTx(const QString& text);
    void    clearTx();

    // Main
    QString getTrxState();
    void    startTx();
    void    startRx();
    void    tune();
    bool    getLock();
    void    setLock(bool enabled);

    // IDs / spot  (fldigi: main.get_txid, main.get_rsid, spot.get_auto)
    bool getTxId();
    void setTxId(bool enabled);
    bool getRxId();
    void setRxId(bool enabled);
    bool getSpot();
    void setSpot(bool enabled);

signals:
    void connectionStateChanged(bool connected);

private slots:
    void attemptReconnect();

private:
    QByteArray buildCall(const QString& method, const QVariantList& params = {}) const;
    QVariant   parseValue(QXmlStreamReader& xml) const;
    QVariant   parseResponse(const QByteArray& data) const;
    QVariant   call(const QString& method, const QVariantList& params = {});

    void onCallSuccess();
    void onCallFailed(const QString& method, const QString& reason);

    bool     m_stubMode;
    bool     m_connected;
    bool     m_callInProgress;  // re-entrancy guard
    int      m_callTimeout;     // ms per call before abort
    int      m_backoffMs;       // current reconnect wait (doubles on each failure)

    QString                m_endpoint;
    QNetworkAccessManager* m_nam;
    QTimer*                m_backoffTimer;
};
