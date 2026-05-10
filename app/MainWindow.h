#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QTimer>
#include <QNetworkAccessManager>

struct TextSizes {
    int rx          = 12;
    int tx          = 12;
    int snrImd      = 11;
    int freq        = 22;
    int sideSection = 9;
    int sideDim     = 10;
    int tabs        = 11;
    int status      = 11;
    int tableHdr    = 10;
    int modePill    = 11;
};

class FLDigiClient;
class WaterfallWidget;
class Sidebar;
class MacroGrid;
class StatusBar;
class FreqDisplay;
class Logbook;
struct LogEntry;
class QTextEdit;
class QLabel;
class QPushButton;
class QAction;
class QEvent;
class QTableWidget;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onPollTimer();
    void onCarrierChanged(int hz);
    void onMacroTriggered(const QString& text);
    void onAfcToggled(bool enabled);
    void onSqlToggled(bool enabled);
    void onReverseToggled(bool enabled);
    void onTuneToggled(bool enabled);
    void onFreqChanged(double hz);
    void onTxToggle();
    void onTxPulse();
    void onConnectionStateChanged(bool connected);
    void onModePillClicked();
    void onSqlLevelChanged(int level);
    void onTrxPollTimer();

    void onRxClear();
    void onLogQso();
    void onClearQso();
    void exportAdif();
    void clearLogbook();
    void editLogEntry(int index);
    void deleteLogEntry(int index);

    void showXmlRpcDialog();
    void showAudioDialog();
    void showEditMacrosDialog();
    void showStationInfoDialog();
    void showCallsignLookupDialog();
    void showTextSizeDialog();
    void showAboutDialog();
    void showFldigiWindow();
    void showFldigiPathDialog();
    void lookupCallsign();
    void toggleTheme(bool dark);

private:
    void buildMenuBar();
    void buildToolbar();
    void buildCentralWidget();
    void applyStylesheet(const QString& resource);
    void setAppPalette(bool dark);
    void loadStationInfo();
    void updateTxButton();
    void stepFrequency(int deltaHz);
    void stepCarrier(int deltaHz);
    void updateCarrierBarPos(int carrierHz);
    void saveFldigiState();
    void restoreFldigiState();
    void refreshLogTable();
    bool showLogEntryDialog(LogEntry& entry, bool isNew);

    void doQrzAuth(const QString& user, const QString& pass, const QString& pendingCall);
    void doQrzLookup(const QString& call);
    void doHamDbLookup(const QString& call);

    FLDigiClient*    m_client;
    WaterfallWidget* m_waterfall;
    Sidebar*         m_sidebar;
    QSplitter*       m_splitter = nullptr;
    QPushButton*     m_sidebarReveal;
    QLabel*          m_carrierLabel = nullptr;
    QWidget*         m_carrierLeftSpacer = nullptr;
    MacroGrid*       m_macroGrid;
    StatusBar*       m_statusBar;
    FreqDisplay*     m_freqDisplay;
    QPushButton*     m_modePill;
    QLabel*          m_callLabel;

    QTextEdit*   m_rxPane;
    QTextEdit*   m_txPane;
    QPushButton* m_txToggleBtn;
    QPushButton* m_rxClearBtn;

    QLabel*  m_snrLabel;
    QAction* m_darkThemeAction;

    QTimer* m_pollTimer;
    QTimer* m_txPulseTimer;
    QTimer* m_trxPollTimer;    // 250ms poll active only during TX or Tune

    QNetworkAccessManager* m_nam = nullptr;
    QString                m_qrzSessionKey;

    bool      m_darkTheme;
    bool      m_isTx;
    bool      m_isTuning;
    bool      m_txPulsePhase;
    TextSizes m_textSizes;

    // Last-polled fldigi state — used to save/restore across sessions
    QString m_lastModem;
    bool    m_lastAfc       = false;
    bool    m_lastSql       = false;
    double  m_lastSqlLevel  = 35.0;
    bool    m_lastReverse   = false;
    bool    m_lastLock      = false;
    bool    m_lastSpot      = false;
    bool    m_lastRxId      = false;
    bool    m_lastTxId      = false;

    // Logbook
    Logbook*      m_logbook;
    QTableWidget* m_logTable;
    QWidget*      m_logbookTab;

    QTabWidget*   m_tabs = nullptr;
};
