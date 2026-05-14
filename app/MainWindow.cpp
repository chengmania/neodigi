#include "MainWindow.h"
#include "FLDigiClient.h"
#include "WaterfallWidget.h"
#include "Sidebar.h"
#include "MacroGrid.h"
#include "StatusBar.h"
#include "FreqDisplay.h"
#include "Logbook.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QMessageBox>
#include <QApplication>
#include <QTextCursor>
#include <QFile>
#include <QSettings>
#include <QStyleFactory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QRadioButton>
#include <QGroupBox>
#include <QPalette>
#include <QListWidget>
#include <QDateTime>
#include <QMouseEvent>
#include <QSplitter>
#include <QDir>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_client(new FLDigiClient(this))
    , m_pollTimer(new QTimer(this))
    , m_txPulseTimer(new QTimer(this))
    , m_trxPollTimer(new QTimer(this))
    , m_liveTxFlushTimer(new QTimer(this))
    , m_darkTheme(true)
    , m_isTx(false)
    , m_isTuning(false)
    , m_txPulsePhase(false)
    , m_logbook(new Logbook(QDir::homePath() + "/.neodigi-fldigi/neodigi_log.json"))
{
    setWindowTitle("neodigi v0.1.0");
    setMinimumSize(900, 600);
    resize(1200, 750);

    {
        QSettings s("KC3SMW", "neodigi");
        m_textSizes.rx          = s.value("textsize/rx",          12).toInt();
        m_textSizes.tx          = s.value("textsize/tx",          12).toInt();
        m_textSizes.snrImd      = s.value("textsize/snrimd",      11).toInt();
        m_textSizes.freq        = s.value("textsize/freq",        22).toInt();
        m_textSizes.sideSection = s.value("textsize/sideSection",  9).toInt();
        m_textSizes.sideDim     = s.value("textsize/sideDim",     10).toInt();
        m_textSizes.tabs        = s.value("textsize/tabs",        11).toInt();
        m_textSizes.status      = s.value("textsize/status",      11).toInt();
        m_textSizes.tableHdr    = s.value("textsize/tableHdr",    10).toInt();
        m_textSizes.modePill    = s.value("textsize/modePill",    11).toInt();
    }

    buildMenuBar();
    buildToolbar();       // m_callLabel and m_modePill initialized here
    buildCentralWidget();
    loadStationInfo();

    // Restore window geometry and tab from last session
    {
        QSettings s("KC3SMW", "neodigi");
        const QByteArray geo = s.value("ui/geometry").toByteArray();
        if (!geo.isEmpty())
            restoreGeometry(geo);
        else
            resize(1200, 750);
        if (m_tabs)
            m_tabs->setCurrentIndex(s.value("ui/tabIndex", 0).toInt());
    }

    // Apply stylesheet with token substitution (overrides main.cpp's unsubstituted load)
    applyStylesheet(":/style.qss");

    // Restore splitter state after the window has its final geometry
    QTimer::singleShot(0, this, [this]() {
        QSettings s("KC3SMW", "neodigi");
        const QByteArray st = s.value("ui/splitterState").toByteArray();
        if (!st.isEmpty()) m_splitter->restoreState(st);
    });

    // ── RX text: polled via XML-RPC rx.get_data (250ms timer below) ──

    // Single-shot poll: reschedules itself at the END of each poll cycle.
    // This prevents the timer from firing again while a blocking XML-RPC
    // call is still waiting inside a nested QEventLoop.
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPollTimer);
    m_pollTimer->start(XMLRPC_POLL_MIN_MS);

    connect(m_txPulseTimer, &QTimer::timeout, this, &MainWindow::onTxPulse);

    // 250ms TRX state poll — runs only while TX or Tune is active
    m_trxPollTimer->setInterval(250);
    m_trxPollTimer->setSingleShot(false);
    connect(m_trxPollTimer, &QTimer::timeout, this, &MainWindow::onTrxPollTimer);

    // 50ms flush — drains m_liveTxPending to fldigi between XML-RPC calls.
    // Fires only while live TX is active. Skips if a call is already in progress
    // to avoid the re-entrancy guard silently dropping the addTx() call.
    m_liveTxFlushTimer->setInterval(50);
    m_liveTxFlushTimer->setSingleShot(false);
    connect(m_liveTxFlushTimer, &QTimer::timeout, this, [this]() {
        if (m_liveTxPending.isEmpty()) return;
        if (m_client->isCallInProgress()) return;
        const QString toSend = m_liveTxPending;
        m_liveTxPending.clear();
        m_client->addTx(toSend);
    });

    connect(m_client, &FLDigiClient::connectionStateChanged,
            this, &MainWindow::onConnectionStateChanged);

    // Waterfall auto-start: try saved source name, fall back gracefully to stub.
    {
        QSettings as("KC3SMW", "neodigi");
        const bool autoStart = as.value("audio/autoStart", true).toBool();
        const QString savedName = as.value("audio/inputDeviceName", "").toString();
        if (autoStart) {
            QTimer::singleShot(3000, this, [this, savedName]() {
                if (!m_waterfall->startAudioSource(savedName))
                    qInfo("[neodigi] Waterfall audio init failed — using stub. "
                          "Go Settings → Waterfall Audio Source… to configure.");
            });
        }
    }
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void MainWindow::buildMenuBar()
{
    // View
    QMenu* viewMenu = menuBar()->addMenu("View");
    m_darkThemeAction = viewMenu->addAction("Dark Theme");
    m_darkThemeAction->setCheckable(true);
    m_darkThemeAction->setChecked(true);
    viewMenu->addAction("Light Theme", this, [this]() { toggleTheme(false); });

    connect(m_darkThemeAction, &QAction::toggled, this, &MainWindow::toggleTheme);

    // Macros
    QMenu* macroMenu = menuBar()->addMenu("Macros");
    macroMenu->addAction("Edit Macros…", this, &MainWindow::showEditMacrosDialog);

    // Settings
    QMenu* settingsMenu = menuBar()->addMenu("Settings");
    settingsMenu->addAction("XML-RPC Connection…",       this, &MainWindow::showXmlRpcDialog);
    settingsMenu->addAction("Waterfall Audio Source…",   this, &MainWindow::showAudioDialog);
    settingsMenu->addAction("Text Size…",                this, &MainWindow::showTextSizeDialog);
    settingsMenu->addSeparator();
    settingsMenu->addAction("Show fldigi Window",        this, &MainWindow::showFldigiWindow);
    settingsMenu->addAction("fldigi Path…",              this, &MainWindow::showFldigiPathDialog);
    settingsMenu->addSeparator();
    settingsMenu->addAction("Station Information…",      this, &MainWindow::showStationInfoDialog);
    settingsMenu->addAction("Callsign Lookup Services…", this, &MainWindow::showCallsignLookupDialog);

    // Help
    QMenu* helpMenu = menuBar()->addMenu("Help");
    helpMenu->addAction("About neodigi…", this, &MainWindow::showAboutDialog);
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void MainWindow::buildToolbar()
{
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setObjectName("MainToolbar");

    // Logo PNG — scaled to 48px height, preserving aspect ratio
    auto* logoLabel = new QLabel;
    logoLabel->setObjectName("appLogo");
    const QPixmap logoPix(":/neodigiIcon.png");
    logoLabel->setPixmap(logoPix.scaledToHeight(96, Qt::SmoothTransformation));
    logoLabel->setContentsMargins(4, 0, 4, 0);
    tb->addWidget(logoLabel);

    tb->addSeparator();

    // Callsign label — bold, initialized from QSettings
    QSettings s("KC3SMW", "neodigi");
    m_callLabel = new QLabel(s.value("station/callsign", "KC3SMW").toString());
    m_callLabel->setObjectName("callLabel");
    QFont callFont = m_callLabel->font();
    callFont.setBold(true);
    m_callLabel->setFont(callFont);
    tb->addWidget(m_callLabel);

    tb->addSeparator();

    // Frequency display with << < > >> step buttons centered below
    auto* freqBox  = new QWidget;
    auto* freqVbox = new QVBoxLayout(freqBox);
    freqVbox->setContentsMargins(0, 2, 0, 2);
    freqVbox->setSpacing(2);

    m_freqDisplay = new FreqDisplay;
    m_freqDisplay->setFrequencyHz(0.0);   // filled in by first poll cycle
    freqVbox->addWidget(m_freqDisplay);

    // Step buttons in a fixed-size widget so Qt::AlignHCenter works correctly
    auto* stepWidget = new QWidget(freqBox);
    auto* stepLayout = new QHBoxLayout(stepWidget);
    stepLayout->setContentsMargins(0, 0, 0, 0);
    stepLayout->setSpacing(2);

    struct { const char* label; int delta; } steps[] = {
        {"<<", -1000}, {"<", -100}, {">", 100}, {">>", 1000}
    };
    for (auto& st : steps) {
        auto* btn = new QPushButton(st.label, stepWidget);
        btn->setObjectName("FreqStepBtn");
        btn->setFixedSize(26, 18);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, [this, d = st.delta]() { stepFrequency(d); });
        stepLayout->addWidget(btn);
    }
    stepWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    freqVbox->addWidget(stepWidget, 0, Qt::AlignHCenter);
    tb->addWidget(freqBox);

    tb->addSeparator();

    // Mode pill — clickable to open mode selector
    m_modePill = new QPushButton("---");
    m_modePill->setObjectName("ModePill");
    m_modePill->setToolTip("Current mode — click to change");
    tb->addWidget(m_modePill);
    connect(m_modePill, &QPushButton::clicked, this, &MainWindow::onModePillClicked);

    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    m_snrLabel = new QLabel("S/N --");
    m_snrLabel->setObjectName("snrLabel");
    tb->addWidget(m_snrLabel);

    connect(m_freqDisplay, &FreqDisplay::frequencyChanged,
            this, &MainWindow::onFreqChanged);
}

// ---------------------------------------------------------------------------
// Central widget
// ---------------------------------------------------------------------------

void MainWindow::buildCentralWidget()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* hbox = new QHBoxLayout(central);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);

    // Narrow reveal tab — visible only when sidebar is hidden
    m_sidebarReveal = new QPushButton("▶", central);
    m_sidebarReveal->setObjectName("SidebarReveal");
    m_sidebarReveal->setFixedWidth(16);
    m_sidebarReveal->setToolTip("Show sidebar");
    m_sidebarReveal->hide();
    hbox->addWidget(m_sidebarReveal);

    // Splitter: sidebar (resizable) | right content
    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setHandleWidth(4);
    m_splitter->setChildrenCollapsible(false);

    m_sidebar = new Sidebar(central);
    m_splitter->addWidget(m_sidebar);

    // Right area
    auto* rightWidget = new QWidget(central);
    auto* vbox = new QVBoxLayout(rightWidget);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    m_waterfall = new WaterfallWidget(rightWidget);
    vbox->addWidget(m_waterfall);

    // Carrier frequency bar — label flanked by << < > >> step buttons
    auto* carrierBar = new QWidget(rightWidget);
    carrierBar->setObjectName("carrierBar");
    carrierBar->setFixedHeight(22);
    auto* carrierHbox = new QHBoxLayout(carrierBar);
    carrierHbox->setContentsMargins(6, 0, 6, 0);
    carrierHbox->setSpacing(3);

    struct { const char* lbl; int delta; } csteps[] = {
        {"<<", -50}, {"<", -10}, {">", 10}, {">>", 50}
    };
    auto makeCarrierBtn = [&](const char* lbl, int delta) -> QPushButton* {
        auto* btn = new QPushButton(lbl, carrierBar);
        btn->setObjectName("FreqStepBtn");
        btn->setFixedSize(24, 16);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, [this, delta]() { stepCarrier(delta); });
        return btn;
    };

    // Left spacer — width updated dynamically to track carrier position on waterfall
    m_carrierLeftSpacer = new QWidget(carrierBar);
    m_carrierLeftSpacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_carrierLeftSpacer->setFixedWidth(0);
    carrierHbox->addWidget(m_carrierLeftSpacer);

    carrierHbox->addWidget(makeCarrierBtn(csteps[0].lbl, csteps[0].delta));
    carrierHbox->addWidget(makeCarrierBtn(csteps[1].lbl, csteps[1].delta));

    m_carrierLabel = new QLabel("◆  --- Hz", carrierBar);
    m_carrierLabel->setObjectName("carrierLabel");
    m_carrierLabel->setAlignment(Qt::AlignCenter);
    m_carrierLabel->setMinimumWidth(80);
    carrierHbox->addWidget(m_carrierLabel);

    carrierHbox->addWidget(makeCarrierBtn(csteps[2].lbl, csteps[2].delta));
    carrierHbox->addWidget(makeCarrierBtn(csteps[3].lbl, csteps[3].delta));
    carrierHbox->addStretch();  // right side fills remaining space
    vbox->addWidget(carrierBar);

    // Tab widget
    m_tabs = new QTabWidget(rightWidget);
    m_tabs->setObjectName("MainTabs");

    // Console tab
    auto* receiveTab    = new QWidget;
    auto* receiveLayout = new QVBoxLayout(receiveTab);
    receiveLayout->setContentsMargins(4, 4, 4, 4);
    receiveLayout->setSpacing(4);

    // RX header row: [RECEIVE] [spacer] [Clear]
    auto* rxHeader  = new QHBoxLayout;
    auto* rxLabel   = new QLabel("RECEIVE");
    rxLabel->setObjectName("dimLabel");

    m_rxClearBtn = new QPushButton("Clear");
    m_rxClearBtn->setFixedSize(52, 24);
    m_rxClearBtn->setToolTip("Clear received text");

    rxHeader->addWidget(rxLabel);
    rxHeader->addStretch();
    rxHeader->addWidget(m_rxClearBtn);

    m_rxPane = new QTextEdit(receiveTab);
    m_rxPane->setObjectName("RxPane");
    m_rxPane->setReadOnly(true);
    m_rxPane->setPlaceholderText("Received text appears here…");
    m_rxPane->installEventFilter(this);

    // TX header row: [TX BUFFER] [spacer] [TX▶ toggle] [Clear]
    auto* txHeader = new QHBoxLayout;
    auto* txLabel  = new QLabel("TX BUFFER");
    txLabel->setObjectName("dimLabel");

    m_txToggleBtn = new QPushButton("▶ TX");
    m_txToggleBtn->setObjectName("TxToggleBtn");
    m_txToggleBtn->setFixedSize(68, 24);
    m_txToggleBtn->setToolTip("Toggle transmit / receive");

    auto* clearTxBtn = new QPushButton("Clear");
    clearTxBtn->setFixedSize(52, 24);

    txHeader->addWidget(txLabel);
    txHeader->addStretch();
    txHeader->addWidget(m_txToggleBtn);
    txHeader->addSpacing(4);

    m_liveTxBtn = new QPushButton("TX/RX");
    m_liveTxBtn->setObjectName("LiveTxBtn");
    m_liveTxBtn->setFixedSize(68, 24);
    m_liveTxBtn->setToolTip("Live TX — stream keystrokes directly to fldigi");
    txHeader->addWidget(m_liveTxBtn);
    txHeader->addSpacing(4);

    txHeader->addWidget(clearTxBtn);

    m_txPane = new QTextEdit(receiveTab);
    m_txPane->setObjectName("TxPane");
    m_txPane->setPlaceholderText("Type outgoing text here…");
    m_txPane->setMaximumHeight(100);

    receiveLayout->addLayout(rxHeader);
    receiveLayout->addWidget(m_rxPane);
    receiveLayout->addLayout(txHeader);
    receiveLayout->addWidget(m_txPane);

    connect(m_rxClearBtn, &QPushButton::clicked, this, &MainWindow::onRxClear);
    connect(clearTxBtn, &QPushButton::clicked, this, [this]() {
        m_txPane->clear();
        m_client->clearTx();
    });
    connect(m_txToggleBtn, &QPushButton::clicked, this, &MainWindow::onTxToggle);
    connect(m_liveTxBtn,  &QPushButton::clicked, this, &MainWindow::onLiveTxToggle);

    connect(m_txPane, &QTextEdit::textChanged, this, [this]() {
        if (!m_isLiveTx) return;
        const QString text = m_txPane->toPlainText();
        if (text.length() <= m_liveTxSentLen) return;  // deletion — ignore
        m_liveTxPending += text.mid(m_liveTxSentLen);
        m_liveTxSentLen = text.length();
    });

    m_tabs->addTab(receiveTab,  "Console");

    // ── Logbook tab ──────────────────────────────────────────────────
    m_logbookTab    = new QWidget;
    auto* logLayout = new QVBoxLayout(m_logbookTab);
    logLayout->setContentsMargins(4, 4, 4, 4);
    logLayout->setSpacing(4);

    // Logbook toolbar
    auto* logBtnRow = new QHBoxLayout;
    auto* exportBtn = new QPushButton("Export ADIF…");
    exportBtn->setFixedHeight(24);
    auto* clearLogBtn = new QPushButton("Clear All");
    clearLogBtn->setFixedHeight(24);
    clearLogBtn->setObjectName("DangerBtn");
    auto* logCountLabel = new QLabel("0 QSOs");
    logCountLabel->setObjectName("dimLabel");
    logBtnRow->addWidget(exportBtn);
    logBtnRow->addWidget(clearLogBtn);
    logBtnRow->addStretch();
    logBtnRow->addWidget(logCountLabel);
    logLayout->addLayout(logBtnRow);

    // Log table
    m_logTable = new QTableWidget(0, 12, m_logbookTab);
    m_logTable->setObjectName("LogTable");
    m_logTable->setAlternatingRowColors(true);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_logTable->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_logTable->verticalHeader()->hide();
    m_logTable->verticalHeader()->setDefaultSectionSize(26);
    m_logTable->setShowGrid(true);

    const QStringList headers = {
        "Date", "Time", "Call", "Band", "Freq", "Mode",
        "RST S", "RST R", "Name", "QTH", "Actions", "Notes"
    };
    m_logTable->setHorizontalHeaderLabels(headers);
    auto* hdr = m_logTable->horizontalHeader();
    hdr->setStretchLastSection(true);
    hdr->setSectionResizeMode(QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(10, QHeaderView::Fixed);
    hdr->setSectionResizeMode(11, QHeaderView::Stretch);
    m_logTable->setColumnWidth(10, 90);

    connect(m_logTable, &QTableWidget::cellChanged,
            this, [this](int row, int col) {
        if (col != 11) return;
        if (row < 0 || row >= m_logbook->entries().size()) return;
        auto* item = m_logTable->item(row, 11);
        if (!item) return;
        const QString newNotes = item->text();
        if (m_logbook->entries()[row].notes != newNotes) {
            m_logbook->entries()[row].notes = newNotes;
            m_logbook->setDirty(true);
        }
    });

    logLayout->addWidget(m_logTable, 1);

    m_tabs->addTab(m_logbookTab, "Logbook");

    connect(exportBtn,   &QPushButton::clicked, this, &MainWindow::exportAdif);
    connect(clearLogBtn, &QPushButton::clicked, this, &MainWindow::clearLogbook);

    vbox->addWidget(m_tabs, 1);

    m_macroGrid = new MacroGrid(rightWidget);
    vbox->addWidget(m_macroGrid);

    m_statusBar = new StatusBar(rightWidget);
    vbox->addWidget(m_statusBar);

    m_splitter->addWidget(rightWidget);
    m_splitter->setStretchFactor(0, 0);  // sidebar: fixed
    m_splitter->setStretchFactor(1, 1);  // right: stretches
    m_splitter->setSizes({180, 10000});   // initial split
    hbox->addWidget(m_splitter, 1);

    // Wire signals
    connect(m_waterfall, &WaterfallWidget::carrierChanged,    this, &MainWindow::onCarrierChanged);
    connect(m_waterfall, &WaterfallWidget::audioLevelChanged, m_sidebar, &Sidebar::setAudioInputLevel);
    connect(m_macroGrid, &MacroGrid::macroTriggered,       this, &MainWindow::onMacroTriggered);
    connect(m_sidebar, &Sidebar::afcToggled,     this, &MainWindow::onAfcToggled);
    connect(m_sidebar, &Sidebar::sqlToggled,     this, &MainWindow::onSqlToggled);
    connect(m_sidebar, &Sidebar::reverseToggled, this, &MainWindow::onReverseToggled);
    connect(m_sidebar, &Sidebar::tuneToggled,    this, &MainWindow::onTuneToggled);
    connect(m_sidebar, &Sidebar::spotToggled,    this, [this](bool e){ m_client->setSpot(e); });
    connect(m_sidebar, &Sidebar::rxIdToggled,    this, [this](bool e){ m_client->setRxId(e); });
    connect(m_sidebar, &Sidebar::txIdToggled,    this, [this](bool e){ m_client->setTxId(e); });
    connect(m_sidebar, &Sidebar::lockToggled,    this, [this](bool e){ m_client->setLock(e); });

    connect(m_statusBar, &StatusBar::sqlLevelChanged, this, &MainWindow::onSqlLevelChanged);
    connect(m_sidebar,   &Sidebar::sqlLevelChanged,   this, &MainWindow::onSqlLevelChanged);

    connect(m_sidebar, &Sidebar::hideRequested, this, [this]() {
        m_sidebar->setVisible(false);
        m_sidebarReveal->setVisible(true);
    });
    connect(m_sidebarReveal, &QPushButton::clicked, this, [this]() {
        m_sidebar->setVisible(true);
        m_sidebarReveal->setVisible(false);
        if (m_splitter->sizes().value(0, 0) < 120)
            m_splitter->setSizes({180, m_splitter->width() - 180});
    });
    connect(m_sidebar, &Sidebar::lookupRequested, this, &MainWindow::lookupCallsign);
    connect(m_sidebar, &Sidebar::logQsoRequested, this, &MainWindow::onLogQso);
    connect(m_sidebar, &Sidebar::clearQsoRequested, this, &MainWindow::onClearQso);

    // Load saved log entries into the table
    refreshLogTable();

    // Defaults until the first poll cycle fires and syncs from fldigi.
    m_statusBar->setModemName("---");
    m_statusBar->setTrxState("--");
    m_statusBar->setSqlLevel(35);
    updateTxButton();
    updateLiveTxButton();
}

// ---------------------------------------------------------------------------
// TX / RX toggle
// ---------------------------------------------------------------------------

void MainWindow::onTxToggle()
{
    m_isTx = !m_isTx;

    if (m_isTx) {
        if (m_isTuning) {
            m_isTuning = false;
            m_sidebar->setTune(false);
        }

        QSettings s("KC3SMW", "neodigi");
        const QString callsign = s.value("station/callsign", "KC3SMW").toString();
        const QString txText   = m_txPane->toPlainText().trimmed();

        QString toSend;
        if (txText.isEmpty()) {
            toSend = "de " + callsign + " ^r";
        } else {
            toSend = txText;
            if (!toSend.endsWith("^r")) {
                if (!toSend.contains(callsign, Qt::CaseInsensitive))
                    toSend += "\n\nde " + callsign;
                toSend += " ^r";
            }
        }

        m_client->addTx(toSend);

        // Echo to RX pane in amber so operator sees what was sent immediately.
        const QString ts = QDateTime::currentDateTime().toString("[yyyy-MM-dd hh:mm] ");
        QTextCursor cur  = m_rxPane->textCursor();
        cur.movePosition(QTextCursor::End);
        QTextCharFormat fmt;
        fmt.setForeground(QColor(0xce, 0x91, 0x78));
        if (!m_rxPane->document()->isEmpty()) cur.insertText("\n", fmt);
        cur.insertText(ts + ">>> " + toSend + "\n", fmt);
        m_rxPane->setTextCursor(cur);
        m_rxPane->ensureCursorVisible();

        m_client->startTx();
        m_txPulseTimer->start(500);
        m_trxPollTimer->start();

        m_txPane->clear();
    } else {
        m_client->startRx();
        m_txPulseTimer->stop();
        m_trxPollTimer->stop();
    }

    m_txPulsePhase = false;
    updateTxButton();
    m_statusBar->setTrxState(m_isTx ? "TX" : "RX");
}

void MainWindow::onTxPulse()
{
    m_txPulsePhase = !m_txPulsePhase;
    if (!m_isTx) return;

    // Pulse between bright red and dim red
    if (m_txPulsePhase) {
        m_txToggleBtn->setStyleSheet(
            "QPushButton { background-color:#f44747; color:#ffffff;"
            " border:1px solid #f44747; border-radius:3px; font-weight:bold; }");
    } else {
        m_txToggleBtn->setStyleSheet(
            "QPushButton { background-color:#7a1515; color:#f44747;"
            " border:1px solid #7a1515; border-radius:3px; font-weight:bold; }");
    }
}

void MainWindow::updateTxButton()
{
    if (m_isTx) {
        m_txToggleBtn->setText("■ RX");
        m_txToggleBtn->setStyleSheet(
            "QPushButton { background-color:#f44747; color:#ffffff;"
            " border:1px solid #f44747; border-radius:3px; font-weight:bold; }"
            "QPushButton:hover { background-color:#ff6060; }");
    } else {
        m_txToggleBtn->setText("▶ TX");
        m_txToggleBtn->setStyleSheet(
            "QPushButton { background-color:#1a4a3a; color:#4ec9b0;"
            " border:1px solid #4ec9b0; border-radius:3px; font-weight:bold; }"
            "QPushButton:hover { background-color:#2a6b5e; }");
    }
}

// ---------------------------------------------------------------------------
// Live TX toggle
// ---------------------------------------------------------------------------

void MainWindow::onLiveTxToggle()
{
    m_isLiveTx = !m_isLiveTx;

    if (m_isLiveTx) {
        if (m_isTuning) {
            m_isTuning = false;
            m_sidebar->setTune(false);
        }
        if (m_isTx) {
            m_isTx = false;
            m_txPulseTimer->stop();
            updateTxButton();
        }
        m_liveTxSentLen = m_txPane->toPlainText().length();
        m_liveTxPending.clear();
        m_client->startTx();
        m_liveTxFlushTimer->start();
        // Do NOT start m_trxPollTimer — live TX is user-controlled, not server-state-driven.
        // The poll timer would reset m_isLiveTx if fldigi returns to RX on an empty buffer.
        m_statusBar->setTrxState("TX");
    } else {
        m_liveTxFlushTimer->stop();
        // Flush any pending text, then close transmission
        if (!m_liveTxPending.isEmpty()) {
            m_client->addTx(m_liveTxPending);
            m_liveTxPending.clear();
        }
        m_client->addTx(" ^r");
        m_liveTxSentLen = 0;
        m_statusBar->setTrxState("RX");
    }

    updateLiveTxButton();
}

void MainWindow::updateLiveTxButton()
{
    if (m_isLiveTx) {
        m_liveTxBtn->setText("● TX/RX");
        m_liveTxBtn->setStyleSheet(
            "QPushButton { background-color:#f44747; color:#ffffff;"
            " border:1px solid #f44747; border-radius:3px; font-weight:bold; }"
            "QPushButton:hover { background-color:#ff6060; }");
    } else {
        m_liveTxBtn->setText("TX/RX");
        m_liveTxBtn->setStyleSheet(
            "QPushButton { background-color:transparent; color:#ce9178;"
            " border:1px solid #ce9178; border-radius:3px; font-weight:bold; }"
            "QPushButton:hover { background-color:#3a2a22; }");
    }
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

void MainWindow::setAppPalette(bool dark)
{
    QPalette p;
    if (dark) {
        p.setColor(QPalette::Window,          QColor(0x1e, 0x1e, 0x1e));
        p.setColor(QPalette::WindowText,      QColor(0xd4, 0xd4, 0xd4));
        p.setColor(QPalette::Base,            QColor(0x2d, 0x2d, 0x2d));
        p.setColor(QPalette::AlternateBase,   QColor(0x25, 0x25, 0x26));
        p.setColor(QPalette::Text,            QColor(0xd4, 0xd4, 0xd4));
        p.setColor(QPalette::Button,          QColor(0x2d, 0x2d, 0x2d));
        p.setColor(QPalette::ButtonText,      QColor(0xd4, 0xd4, 0xd4));
        p.setColor(QPalette::Highlight,       QColor(0x3a, 0x5f, 0x7a));
        p.setColor(QPalette::HighlightedText, QColor(0xd4, 0xd4, 0xd4));
        p.setColor(QPalette::Mid,             QColor(0x3c, 0x3c, 0x3c));
        p.setColor(QPalette::Dark,            QColor(0x18, 0x18, 0x18));
        p.setColor(QPalette::PlaceholderText, QColor(0x6a, 0x6a, 0x6a));
    } else {
        p = QApplication::style()->standardPalette();
    }
    qApp->setPalette(p);
}

void MainWindow::applyStylesheet(const QString& resource)
{
    QFile f(resource);
    if (!f.open(QFile::ReadOnly)) return;
    QString css = QString::fromUtf8(f.readAll());

    css.replace("@@rx@@",          QString::number(m_textSizes.rx));
    css.replace("@@tx@@",          QString::number(m_textSizes.tx));
    css.replace("@@snrimd@@",      QString::number(m_textSizes.snrImd));
    css.replace("@@freq@@",        QString::number(m_textSizes.freq));
    css.replace("@@sideSection@@", QString::number(m_textSizes.sideSection));
    css.replace("@@sideDim@@",     QString::number(m_textSizes.sideDim));
    css.replace("@@tabs@@",        QString::number(m_textSizes.tabs));
    css.replace("@@status@@",      QString::number(m_textSizes.status));
    css.replace("@@tableHdr@@",    QString::number(m_textSizes.tableHdr));
    css.replace("@@modePill@@",    QString::number(m_textSizes.modePill));

    qApp->setStyleSheet(css);
}

void MainWindow::toggleTheme(bool dark)
{
    m_darkTheme = dark;
    m_darkThemeAction->setChecked(dark);
    setAppPalette(dark);
    applyStylesheet(dark ? ":/style.qss" : ":/light.qss");
    updateTxButton();  // re-apply inline styles after QSS change
}

// ---------------------------------------------------------------------------
// Station info
// ---------------------------------------------------------------------------

void MainWindow::loadStationInfo()
{
    QSettings s("KC3SMW", "neodigi");
    // Update toolbar callsign label
    const QString call = s.value("station/callsign", "KC3SMW").toString();
    m_callLabel->setText(call.isEmpty() ? "KC3SMW" : call);
    // Other fields are in QSettings but not pushed to sidebar automatically;
    // sidebar QSO fields are for per-QSO info, not station config.
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

void MainWindow::showXmlRpcDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("XML-RPC Connection");
    dlg.setFixedSize(320, 130);

    auto* form     = new QFormLayout(&dlg);
    auto* hostEdit = new QLineEdit(XMLRPC_HOST, &dlg);
    auto* portSpin = new QSpinBox(&dlg);
    portSpin->setRange(1, 65535);
    portSpin->setValue(XMLRPC_PORT);
    form->addRow("Host:", hostEdit);
    form->addRow("Port:", portSpin);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        m_client->setEndpoint(hostEdit->text(), portSpin->value());
        m_client->setStubMode(false);
    }
}

void MainWindow::showTextSizeDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Text Size");
    dlg.setMinimumWidth(360);

    auto* outer = new QVBoxLayout(&dlg);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto makeSpin = [&](int val, int lo, int hi) -> QSpinBox* {
        auto* sb = new QSpinBox;
        sb->setRange(lo, hi);
        sb->setValue(val);
        sb->setSuffix(" px");
        sb->setFixedWidth(80);
        return sb;
    };

    auto* rxSpin     = makeSpin(m_textSizes.rx,          8, 32);
    auto* txSpin     = makeSpin(m_textSizes.tx,          8, 32);
    auto* snrimdSpin = makeSpin(m_textSizes.snrImd,      7, 24);
    auto* freqSpin   = makeSpin(m_textSizes.freq,        12, 48);
    auto* secSpin    = makeSpin(m_textSizes.sideSection,  7, 18);
    auto* dimSpin    = makeSpin(m_textSizes.sideDim,      7, 18);
    auto* tabsSpin   = makeSpin(m_textSizes.tabs,         7, 18);
    auto* statusSpin = makeSpin(m_textSizes.status,       7, 18);
    auto* hdrSpin    = makeSpin(m_textSizes.tableHdr,     7, 18);
    auto* pillSpin   = makeSpin(m_textSizes.modePill,     7, 18);

    form->addRow("RX pane text:",      rxSpin);
    form->addRow("TX pane text:",      txSpin);
    form->addRow("S/N label:",         snrimdSpin);
    form->addRow("Frequency display:", freqSpin);
    form->addRow("Sidebar headers:",   secSpin);
    form->addRow("Sidebar labels:",    dimSpin);
    form->addRow("Tab labels:",        tabsSpin);
    form->addRow("Status bar text:",   statusSpin);
    form->addRow("Table headers:",     hdrSpin);
    form->addRow("Mode pill:",         pillSpin);

    outer->addLayout(form);
    outer->addSpacing(8);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto* restoreBtn = btns->addButton("Restore Defaults", QDialogButtonBox::ResetRole);
    outer->addWidget(btns);

    connect(restoreBtn, &QPushButton::clicked, this, [&]() {
        rxSpin->setValue(12);     txSpin->setValue(12);
        snrimdSpin->setValue(11); freqSpin->setValue(22);
        secSpin->setValue(9);     dimSpin->setValue(10);
        tabsSpin->setValue(11);   statusSpin->setValue(11);
        hdrSpin->setValue(10);    pillSpin->setValue(11);
    });
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        m_textSizes.rx          = rxSpin->value();
        m_textSizes.tx          = txSpin->value();
        m_textSizes.snrImd      = snrimdSpin->value();
        m_textSizes.freq        = freqSpin->value();
        m_textSizes.sideSection = secSpin->value();
        m_textSizes.sideDim     = dimSpin->value();
        m_textSizes.tabs        = tabsSpin->value();
        m_textSizes.status      = statusSpin->value();
        m_textSizes.tableHdr    = hdrSpin->value();
        m_textSizes.modePill    = pillSpin->value();

        QSettings s("KC3SMW", "neodigi");
        s.setValue("textsize/rx",          m_textSizes.rx);
        s.setValue("textsize/tx",          m_textSizes.tx);
        s.setValue("textsize/snrimd",      m_textSizes.snrImd);
        s.setValue("textsize/freq",        m_textSizes.freq);
        s.setValue("textsize/sideSection", m_textSizes.sideSection);
        s.setValue("textsize/sideDim",     m_textSizes.sideDim);
        s.setValue("textsize/tabs",        m_textSizes.tabs);
        s.setValue("textsize/status",      m_textSizes.status);
        s.setValue("textsize/tableHdr",    m_textSizes.tableHdr);
        s.setValue("textsize/modePill",    m_textSizes.modePill);

        applyStylesheet(m_darkTheme ? ":/style.qss" : ":/light.qss");
        updateTxButton();
    }
}

void MainWindow::showAudioDialog()
{
    const auto devices = WaterfallWidget::inputDevices();

    QDialog dlg(this);
    dlg.setWindowTitle("Waterfall Audio Source");
    dlg.setMinimumWidth(460);

    auto* form  = new QFormLayout(&dlg);

    auto* warn = new QLabel(
        "<b>PulseAudio</b> — shares the audio device with fldigi.<br>"
        "Enable to see the live RF spectrum waterfall.", &dlg);
    warn->setWordWrap(true);
    warn->setObjectName("dimLabel");
    form->addRow(warn);

    auto* combo = new QComboBox(&dlg);
    combo->addItem("Disabled (stub waterfall)");
    for (const auto& d : devices)
        combo->addItem(d.second, d.first);  // display: description, data: source name

    QSettings as("KC3SMW", "neodigi");
    // Pre-select based on actual waterfall state, not just saved settings
    if (!m_waterfall->isStubMode()) {
        const QString savedName = as.value("audio/inputDeviceName", "").toString();
        bool found = false;
        if (!savedName.isEmpty()) {
            for (int i = 0; i < devices.size(); ++i) {
                if (devices[i].first == savedName) {
                    combo->setCurrentIndex(i + 1);
                    found = true;
                    break;
                }
            }
        }
        if (!found) combo->setCurrentIndex(1);  // "Default" — auto-detect
    } else {
        combo->setCurrentIndex(0);  // Disabled
    }
    form->addRow("Input source:", combo);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        if (combo->currentIndex() == 0) {
            // Disabled — stub waterfall
            as.setValue("audio/autoStart", false);
            as.setValue("audio/inputDeviceName", "");
            m_waterfall->setStubMode(true);
        } else {
            // devices[0]="Default" (name=""), devices[1+]=real sources
            const QString srcName = devices[combo->currentIndex() - 1].first;
            as.setValue("audio/inputDeviceName", srcName);
            as.setValue("audio/autoStart", true);
            if (!m_waterfall->startAudioSource(srcName))
                QMessageBox::warning(this, "Audio Source",
                    "Could not open the selected audio source.\n\n"
                    "The device may be unavailable. The waterfall will\n"
                    "try auto-detect on the next launch.");
        }
    }
}

void MainWindow::showStationInfoDialog()
{
    QSettings s("KC3SMW", "neodigi");

    QDialog dlg(this);
    dlg.setWindowTitle("Station Information");
    dlg.setMinimumWidth(400);

    auto* form     = new QFormLayout(&dlg);
    auto* nameEdit = new QLineEdit(s.value("station/opname", "").toString(), &dlg);
    auto* callEdit = new QLineEdit(s.value("station/callsign", "").toString(), &dlg);
    auto* qthEdit  = new QLineEdit(s.value("station/qth", "").toString(), &dlg);
    auto* gridEdit = new QLineEdit(s.value("station/grid", "").toString(), &dlg);
    auto* rigEdit  = new QLineEdit(s.value("station/rig", "").toString(), &dlg);
    auto* antEdit  = new QLineEdit(s.value("station/antenna", "").toString(), &dlg);
    auto* pwrEdit  = new QLineEdit(s.value("station/power", "").toString(), &dlg);
    auto* parkEdit = new QLineEdit(s.value("station/park", "").toString(), &dlg);

    callEdit->setPlaceholderText("e.g. KC3SMW");
    gridEdit->setPlaceholderText("e.g. FN10");
    parkEdit->setPlaceholderText("e.g. K-1234");

    form->addRow("Operator Name:", nameEdit);
    form->addRow("Callsign:",      callEdit);
    form->addRow("QTH:",           qthEdit);
    form->addRow("Grid Square:",   gridEdit);
    form->addRow("Rig:",           rigEdit);
    form->addRow("Antenna:",       antEdit);
    form->addRow("Power (W):",     pwrEdit);
    form->addRow("POTA Park:",     parkEdit);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        s.setValue("station/opname",   nameEdit->text());
        s.setValue("station/callsign", callEdit->text());
        s.setValue("station/qth",      qthEdit->text());
        s.setValue("station/grid",     gridEdit->text());
        s.setValue("station/rig",      rigEdit->text());
        s.setValue("station/antenna",  antEdit->text());
        s.setValue("station/power",    pwrEdit->text());
        s.setValue("station/park",     parkEdit->text());

        // Update toolbar callsign label
        m_callLabel->setText(callEdit->text().isEmpty() ? "KC3SMW" : callEdit->text());
    }
}

void MainWindow::showCallsignLookupDialog()
{
    QSettings s("KC3SMW", "neodigi");
    const QString savedService = s.value("lookup/service", "auto").toString();

    QDialog dlg(this);
    dlg.setWindowTitle("Callsign Lookup Services");
    dlg.setMinimumWidth(400);

    auto* outer = new QVBoxLayout(&dlg);
    outer->setSpacing(6);

    // ── Service selector ──────────────────────────────────────────────────
    auto* svcLabel = new QLabel("LOOKUP SERVICE", &dlg);
    svcLabel->setObjectName("sectionLabel");
    outer->addWidget(svcLabel);

    auto* radioAuto  = new QRadioButton("Auto — QRZ first, HamDB fallback", &dlg);
    auto* radioQrz   = new QRadioButton("QRZ.com only",                     &dlg);
    auto* radioHamdb = new QRadioButton("HamDB only  (free, no login)",      &dlg);

    if      (savedService == "qrz")   radioQrz->setChecked(true);
    else if (savedService == "hamdb") radioHamdb->setChecked(true);
    else                              radioAuto->setChecked(true);

    outer->addWidget(radioAuto);
    outer->addWidget(radioQrz);
    outer->addWidget(radioHamdb);
    outer->addSpacing(8);

    // ── QRZ credentials ───────────────────────────────────────────────────
    auto* credLabel = new QLabel("QRZ.COM CREDENTIALS", &dlg);
    credLabel->setObjectName("sectionLabel");
    outer->addWidget(credLabel);

    auto* subNote = new QLabel("XML data subscription required.", &dlg);
    subNote->setObjectName("dimLabel");
    outer->addWidget(subNote);
    outer->addSpacing(4);

    auto* credForm = new QFormLayout;
    credForm->setLabelAlignment(Qt::AlignRight);

    auto* qrzUser = new QLineEdit(s.value("qrz/username").toString(), &dlg);
    auto* qrzPass = new QLineEdit(s.value("qrz/password").toString(), &dlg);
    qrzPass->setEchoMode(QLineEdit::Password);
    qrzUser->setPlaceholderText("username");
    qrzPass->setPlaceholderText("password");
    credForm->addRow("Login:",    qrzUser);
    credForm->addRow("Password:", qrzPass);
    outer->addLayout(credForm);
    outer->addSpacing(8);

    // Grey out credentials when HamDB-only is selected
    auto updateCredState = [&]() {
        const bool needQrz = !radioHamdb->isChecked();
        credLabel->setEnabled(needQrz);
        subNote->setEnabled(needQrz);
        qrzUser->setEnabled(needQrz);
        qrzPass->setEnabled(needQrz);
    };
    connect(radioAuto,  &QRadioButton::toggled, this, [updateCredState](bool){ updateCredState(); });
    connect(radioQrz,   &QRadioButton::toggled, this, [updateCredState](bool){ updateCredState(); });
    connect(radioHamdb, &QRadioButton::toggled, this, [updateCredState](bool){ updateCredState(); });
    updateCredState();

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    outer->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        const QString svc = radioQrz->isChecked()  ? "qrz"
                          : radioHamdb->isChecked() ? "hamdb"
                          : "auto";
        s.setValue("lookup/service", svc);
        s.setValue("qrz/username",   qrzUser->text().trimmed());
        s.setValue("qrz/password",   qrzPass->text());
        m_qrzSessionKey.clear();
    }
}

void MainWindow::showEditMacrosDialog()
{
    m_macroGrid->openFullEditor(this);
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, "About neodigi",
        "<b>neodigi v0.1.0</b><br>"
        "Modern Qt6 UI for fldigi modem<br><br>"
        "Author: <b>chengmania KC3SMW</b><br>"
        "License: GPL v3<br><br>"
        "fldigi modem engine communicates via XML-RPC on port 7362.");
}

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------

void MainWindow::onConnectionStateChanged(bool connected)
{
    if (connected) {
        m_modePill->setText("...");
        m_statusBar->setModemName("...");
        qInfo("[neodigi] fldigi connected");
        // Defer restore until after the current call() returns (m_callInProgress clears)
        QTimer::singleShot(200, this, &MainWindow::restoreFldigiState);
    } else {
        m_modePill->setText("---");
        m_statusBar->setModemName("NO CONN");
        m_statusBar->setTrxState("--");
        // Clean up live TX if fldigi goes away
        if (m_isLiveTx) {
            m_isLiveTx = false;
            m_liveTxSentLen = 0;
            m_liveTxPending.clear();
            m_liveTxFlushTimer->stop();
            updateLiveTxButton();
        }
        qInfo("[neodigi] fldigi disconnected — waiting for reconnect");
    }
}

// ---------------------------------------------------------------------------
// Poll timer  (single-shot — always reschedules itself at the very end)
// ---------------------------------------------------------------------------

void MainWindow::onPollTimer()
{
    // When disconnected the backoff timer in FLDigiClient handles reconnect.
    // We still reschedule the poll so it picks up immediately after reconnect.
    if (!m_client->isConnected() && !m_client->isStubMode()) {
        m_pollTimer->start(XMLRPC_POLL_MIN_MS);
        return;
    }

    // RX decoded text (polled here to avoid re-entrancy drops from independent timers)
    {
        QString rx = m_client->getRxData();
        if (!rx.isEmpty()) {
            // Strip control chars (except \n, \r, \t) to match fldigi's display
            for (int i = 0; i < rx.size();) {
                const QChar c = rx.at(i);
                if (c.unicode() < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                    rx.remove(i, 1);
                } else {
                    ++i;
                }
            }
            {
                QTextCursor cur = m_rxPane->textCursor();
                cur.movePosition(QTextCursor::End);
                QTextCharFormat fmt;
                fmt.setForeground(QColor(0x4e, 0xc9, 0xb0));  // green — reset from any TX echo amber
                cur.setCharFormat(fmt);
                cur.insertText(rx);
                m_rxPane->setTextCursor(cur);
                m_rxPane->ensureCursorVisible();
            }
        }
    }

    // Mode name
    const QString modemName = m_client->getModemName();
    if (!modemName.isEmpty()) {
        m_lastModem = modemName;
        m_modePill->setText(modemName);
        m_statusBar->setModemName(modemName);
    }

    // TRX state sync — poll actual fldigi state and update local flags
    if (!m_client->isStubMode()) {
        const bool serverTx = (m_client->getTrxState() == "TX");

        // TX button: drop TX when fldigi finishes sending (goes back to RX)
        if (m_isTx && !serverTx) {
            m_isTx = false;
            updateTxButton();
            m_txPulseTimer->stop();
        }

        // Tune button: fldigi returned to RX — cancel tune indicator
        if (m_isTuning && !serverTx) {
            m_isTuning = false;
            m_sidebar->setTune(false);
        }

        m_statusBar->setTrxState((serverTx || m_isLiveTx) ? "TX" : "RX");
    } else {
        m_statusBar->setTrxState((m_isTx || m_isLiveTx) ? "TX" : "RX");
    }

    // VFO frequency
    m_freqDisplay->setFrequencyHz(m_client->getRigFrequency());

    // Waterfall carrier cursor + carrier label
    const int carrierHz = m_client->getCarrier();
    m_waterfall->setCarrierHz(carrierHz);
    m_carrierLabel->setText(carrierHz > 0
        ? QString("◆  %1 Hz").arg(carrierHz)
        : "◆  --- Hz");
    updateCarrierBarPos(carrierHz);

    // Signal quality → S/N label + meters
    const double quality   = m_client->getQuality();
    const int    signalPct = static_cast<int>(quality * 100.0);
    const int    snrDb     = static_cast<int>(quality * 30.0);
    m_snrLabel->setText(snrDb > 0 ? QString("S/N %1dB").arg(snrDb) : "S/N --");
    m_statusBar->setSignalLevel(signalPct);
    // Audio input level comes from WaterfallWidget::audioLevelChanged (real RMS).
    // Noise meter: set proportional to squelch quality (rough proxy until real noise floor)
    m_sidebar->setNoiseLevel(signalPct / 4);

    // Toggle button state sync (QSignalBlocker inside Sidebar setters prevents feedback)
    m_lastAfc = m_client->getAfc();
    m_sidebar->setAfc(m_lastAfc);
    m_statusBar->setAfc(m_lastAfc);

    m_lastReverse = m_client->getReverse();
    m_sidebar->setReverse(m_lastReverse);
    m_statusBar->setReverse(m_lastReverse);

    m_lastSql = m_client->getSquelchOnOff();
    m_sidebar->setSql(m_lastSql);

    m_lastLock = m_client->getLock();
    m_sidebar->setLock(m_lastLock);

    // IDs / spot LED sync
    m_lastTxId = m_client->getTxId();
    m_sidebar->setTxId(m_lastTxId);

    m_lastRxId = m_client->getRxId();
    m_sidebar->setRxId(m_lastRxId);

    m_lastSpot = m_client->getSpot();
    m_sidebar->setSpot(m_lastSpot);

    // Squelch level — feedback is harmless (fldigi gets same value back)
    m_lastSqlLevel = m_client->getSquelch();
    m_statusBar->setSqlLevel(static_cast<int>(m_lastSqlLevel));

    // Reschedule — only after all calls above have returned
    m_pollTimer->start(XMLRPC_POLL_MIN_MS);
}

// ---------------------------------------------------------------------------
// Mode pill
// ---------------------------------------------------------------------------

void MainWindow::onModePillClicked()
{
    const QStringList names = m_client->getModemNames();
    if (names.isEmpty()) {
        QMessageBox::information(this, "Mode Selector",
            "Could not retrieve modem list from fldigi.");
        return;
    }
    const QString current = m_modePill->text();
    ModeSelector dlg(names, current, this);
    connect(&dlg, &ModeSelector::modeSelected, this, [this](const QString& mode) {
        m_client->setModemByName(mode);
        m_modePill->setText(mode);
        m_statusBar->setModemName(mode);
    });
    dlg.exec();
}

// ---------------------------------------------------------------------------
// SQL level from status bar slider
// ---------------------------------------------------------------------------

void MainWindow::onSqlLevelChanged(int level)
{
    m_client->setSquelch(static_cast<double>(level));
}

// ---------------------------------------------------------------------------
// Event filter — double-click in RX pane extracts callsign
// ---------------------------------------------------------------------------

bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_rxPane && ev->type() == QEvent::MouseButtonDblClick) {
        auto* me = static_cast<QMouseEvent*>(ev);
        QTextCursor cursor = m_rxPane->cursorForPosition(me->pos());
        cursor.select(QTextCursor::WordUnderCursor);
        const QString word = cursor.selectedText().toUpper().trimmed();
        if (word.length() >= 3 && word.length() <= 9) {
            bool valid = true, hasLetter = false;
            for (const QChar& c : word) {
                if (!c.isLetterOrNumber() && c != '/') { valid = false; break; }
                if (c.isLetter()) hasLetter = true;
            }
            if (valid && hasLetter) {
                m_sidebar->setCallsign(word);
                lookupCallsign();
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

// ---------------------------------------------------------------------------
// Misc slots
// ---------------------------------------------------------------------------

void MainWindow::onFreqChanged(double hz)
{
    m_client->setRigFrequency(hz);
}

void MainWindow::onCarrierChanged(int hz)
{
    m_client->setCarrier(hz);
    m_waterfall->setCarrierHz(hz);
}

void MainWindow::onMacroTriggered(const QString& text)
{
    m_txPane->insertPlainText(text);
    m_client->addTx(text);
}

void MainWindow::onAfcToggled(bool enabled)
{
    m_client->setAfc(enabled);
    m_statusBar->setAfc(enabled);
}

void MainWindow::onSqlToggled(bool enabled)
{
    m_client->setSquelchOnOff(enabled);
}

void MainWindow::onReverseToggled(bool enabled)
{
    m_client->setReverse(enabled);
    m_statusBar->setReverse(enabled);
}

void MainWindow::onTuneToggled(bool enabled)
{
    m_isTuning = enabled;
    if (enabled) {
        // Drop TX state if we're currently transmitting
        if (m_isTx) {
            m_isTx = false;
            updateTxButton();
            m_txPulseTimer->stop();
        }
        m_client->tune();
        m_trxPollTimer->start();  // 250ms poll — deactivates when fldigi returns to RX
    } else {
        m_client->startRx();
        if (!m_isTx) m_trxPollTimer->stop();
    }
}

// ---------------------------------------------------------------------------
// Dedicated 250ms TRX state poll — runs only while TX or Tune is active
// ---------------------------------------------------------------------------

void MainWindow::onTrxPollTimer()
{
    if (!m_client->isConnected() && !m_client->isStubMode()) return;

    const bool serverTx = (m_client->getTrxState() == QLatin1String("TX"));

    // TX finished: fldigi returned to RX after sending buffer
    if (m_isTx && !serverTx) {
        m_isTx = false;
        updateTxButton();
        m_txPulseTimer->stop();
        m_statusBar->setTrxState("RX");
    }

    // Tune finished: fldigi returned to RX
    if (m_isTuning && !serverTx) {
        m_isTuning = false;
        m_sidebar->setTune(false);
    }

    // Nothing left to monitor — stop the fast poll
    if (!m_isTx && !m_isTuning)
        m_trxPollTimer->stop();
}

// ---------------------------------------------------------------------------
// Frequency step buttons
// ---------------------------------------------------------------------------

void MainWindow::stepFrequency(int deltaHz)
{
    const double newHz = qMax(0.0, m_freqDisplay->frequencyHz() + deltaHz);
    m_freqDisplay->setFrequencyHz(newHz);
    m_client->setRigFrequency(newHz);
}

void MainWindow::stepCarrier(int deltaHz)
{
    const int current = m_client->getCarrier();
    const int newHz   = qBound(100, current + deltaHz, 3900);
    m_client->setCarrier(newHz);
    m_waterfall->setCarrierHz(newHz);
    m_carrierLabel->setText(QString("◆  %1 Hz").arg(newHz));
    updateCarrierBarPos(newHz);
}

void MainWindow::updateCarrierBarPos(int carrierHz)
{
    if (!m_carrierLeftSpacer || !m_waterfall) return;
    const int wfW = m_waterfall->width();
    if (wfW <= 0 || carrierHz <= 0) return;
    // Approx content width: 4 buttons×24 + 4 gaps×3 + label~90 = 198px
    constexpr int kContentW = 198;
    const int centerX = static_cast<int>(carrierHz / 4000.0 * wfW);
    const int leftPx  = qMax(0, centerX - kContentW / 2);
    m_carrierLeftSpacer->setFixedWidth(leftPx);
}

// ---------------------------------------------------------------------------
// State save / restore across sessions
// ---------------------------------------------------------------------------

void MainWindow::saveFldigiState()
{
    if (m_lastModem.isEmpty() || m_lastModem == "---" || m_lastModem == "...")
        return;  // never connected this session — don't overwrite prior save
    QSettings s("KC3SMW", "neodigi");
    s.setValue("fldigi/modem",        m_lastModem);
    s.setValue("fldigi/afc",          m_lastAfc);
    s.setValue("fldigi/squelch",      m_lastSql);
    s.setValue("fldigi/squelchLevel", m_lastSqlLevel);
    s.setValue("fldigi/reverse",      m_lastReverse);
    s.setValue("fldigi/lock",         m_lastLock);
    s.setValue("fldigi/spot",         m_lastSpot);
    s.setValue("fldigi/rxid",         m_lastRxId);
    s.setValue("fldigi/txid",         m_lastTxId);
}

void MainWindow::restoreFldigiState()
{
    QSettings s("KC3SMW", "neodigi");
    if (!s.contains("fldigi/modem")) return;  // first run — nothing saved yet

    const QString mode = s.value("fldigi/modem").toString();
    if (!mode.isEmpty())
        m_client->setModemByName(mode);

    m_client->setAfc(         s.value("fldigi/afc",          false).toBool());
    m_client->setSquelchOnOff(s.value("fldigi/squelch",      false).toBool());
    m_client->setSquelch(     s.value("fldigi/squelchLevel",  35.0).toDouble());
    m_client->setReverse(     s.value("fldigi/reverse",      false).toBool());
    m_client->setLock(        s.value("fldigi/lock",         false).toBool());
    m_client->setSpot(        s.value("fldigi/spot",         false).toBool());
    m_client->setRxId(        s.value("fldigi/rxid",         false).toBool());
    m_client->setTxId(        s.value("fldigi/txid",         false).toBool());
}

void MainWindow::showFldigiWindow()
{
    QMessageBox::information(this, "fldigi",
        "Find fldigi in your taskbar and click it to show it.");
}

void MainWindow::lookupCallsign()
{
    const QString call = m_sidebar->callsign().trimmed().toUpper();
    if (call.isEmpty()) return;
    if (!m_nam) m_nam = new QNetworkAccessManager(this);

    QSettings s("KC3SMW", "neodigi");
    const QString service = s.value("lookup/service", "auto").toString();
    const QString user    = s.value("qrz/username").toString().trimmed();
    const QString pass    = s.value("qrz/password").toString();

    const bool useQrz   = (service == "qrz" || service == "auto") &&
                          !user.isEmpty() && !pass.isEmpty();

    if (service == "hamdb" || !useQrz) {
        doHamDbLookup(call);
    } else {
        if (m_qrzSessionKey.isEmpty())
            doQrzAuth(user, pass, call);
        else
            doQrzLookup(call);
    }
}

void MainWindow::doQrzAuth(const QString& user, const QString& pass,
                           const QString& pendingCall)
{
    QUrl url("https://xmldata.qrz.com/xml/current/");
    QUrlQuery q;
    q.addQueryItem("username", user);
    q.addQueryItem("password", pass);
    q.addQueryItem("agent",    "neodigi");
    url.setQuery(q);

    QNetworkReply* reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, pendingCall]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("[QRZ] Auth network error: %s",
                     reply->errorString().toUtf8().constData());
            doHamDbLookup(pendingCall);
            return;
        }
        const QByteArray data = reply->readAll();

        // Extract session key from <Key> element
        QXmlStreamReader xml(data);
        QString key, error;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.name() == QLatin1String("Key"))
                    key = xml.readElementText();
                else if (xml.name() == QLatin1String("Error"))
                    error = xml.readElementText();
            }
        }

        if (key.isEmpty()) {
            qWarning("[QRZ] Auth failed: %s",
                     error.isEmpty() ? "no session key returned"
                                     : error.toUtf8().constData());
            doHamDbLookup(pendingCall);
            return;
        }
        m_qrzSessionKey = key;
        qInfo("[QRZ] Authenticated OK");
        doQrzLookup(pendingCall);
    });
}

void MainWindow::doQrzLookup(const QString& call)
{
    QUrl url("https://xmldata.qrz.com/xml/current/");
    QUrlQuery q;
    q.addQueryItem("s",        m_qrzSessionKey);
    q.addQueryItem("callsign", call);
    url.setQuery(q);

    QNetworkReply* reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, call]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("[QRZ] Lookup network error: %s",
                     reply->errorString().toUtf8().constData());
            doHamDbLookup(call);
            return;
        }
        const QByteArray data = reply->readAll();

        // Parse <Callsign> fields and check for session/error
        QMap<QString, QString> fields;
        QString error;
        bool inCallsign = false;
        QXmlStreamReader xml(data);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const auto tag = xml.name();
                if (tag == QLatin1String("Callsign"))       inCallsign = true;
                else if (tag == QLatin1String("Error"))     error = xml.readElementText();
                else if (inCallsign)                        fields[tag.toString()] = xml.readElementText();
            } else if (xml.isEndElement() &&
                       xml.name() == QLatin1String("Callsign")) {
                inCallsign = false;
            }
        }

        // Session expired → clear key and fall through to HamDB
        if (!error.isEmpty() && error.contains("ession", Qt::CaseInsensitive)) {
            qInfo("[QRZ] Session expired — will re-auth on next lookup");
            m_qrzSessionKey.clear();
            doHamDbLookup(call);
            return;
        }

        if (fields.isEmpty()) {
            QSettings s("KC3SMW", "neodigi");
            if (s.value("lookup/service", "auto").toString() == "auto") {
                qInfo("[QRZ] No record for %s — trying HamDB", call.toUtf8().constData());
                doHamDbLookup(call);
            } else {
                qInfo("[QRZ] No record for %s", call.toUtf8().constData());
            }
            return;
        }

        const QString fname   = fields.value("fname").trimmed();
        const QString lname   = fields.value("name").trimmed();
        const QString city    = fields.value("addr2").trimmed();
        const QString state   = fields.value("state").trimmed();
        const QString country = fields.value("country").trimmed();
        const QString grid    = fields.value("grid").trimmed();

        const QString name = (fname + " " + lname).trimmed();
        QString qth = city.isEmpty() ? state
                    : state.isEmpty() ? city
                    : city + ", " + state;
        if (!country.isEmpty() && country != "USA")
            qth += (qth.isEmpty() ? "" : " · ") + country;

        if (!name.isEmpty()) m_sidebar->setOperatorName(name);
        if (!qth.isEmpty())  m_sidebar->setQth(qth);
        if (!grid.isEmpty()) m_sidebar->setGrid(grid);

        qInfo("[QRZ] %s → %s, %s%s",
              call.toUtf8().constData(),
              name.toUtf8().constData(),
              qth.toUtf8().constData(),
              grid.isEmpty() ? "" : (" [" + grid + "]").toUtf8().constData());
    });
}

void MainWindow::doHamDbLookup(const QString& call)
{
    QNetworkRequest req(
        QUrl(QString("http://api.hamdb.org/v1/%1/json/neodigi").arg(call)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "neodigi/0.1.0");

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, call]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("[HamDB] Request failed: %s",
                     reply->errorString().toUtf8().constData());
            return;
        }
        const QJsonObject root   = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject hamdb  = root["hamdb"].toObject();
        const QJsonObject record = hamdb["callsign"].toObject();
        if (hamdb["messages"].toObject()["status"].toString() != "OK" || record.isEmpty()) {
            qInfo("[HamDB] No record for %s", call.toUtf8().constData());
            return;
        }
        const QString fname = record["fname"].toString().trimmed();
        const QString lname = record["name"].toString().trimmed();
        const QString city  = record["addr2"].toString().trimmed();
        const QString state = record["state"].toString().trimmed();
        const QString grid  = record["grid"].toString().trimmed();

        const QString name = (fname + " " + lname).trimmed();
        const QString qth  = city.isEmpty() ? state
                           : state.isEmpty() ? city
                           : city + ", " + state;

        if (!name.isEmpty()) m_sidebar->setOperatorName(name);
        if (!qth.isEmpty())  m_sidebar->setQth(qth);
        if (!grid.isEmpty()) m_sidebar->setGrid(grid);

        qInfo("[HamDB] %s → %s, %s",
              call.toUtf8().constData(),
              name.toUtf8().constData(),
              qth.toUtf8().constData());
    });
}

// ---------------------------------------------------------------------------
// RX Clear
// ---------------------------------------------------------------------------

void MainWindow::onRxClear()
{
    m_rxPane->clear();
    m_client->clearRx();
}

// ---------------------------------------------------------------------------
// QSO actions
// ---------------------------------------------------------------------------

void MainWindow::onLogQso()
{
    const QString call = m_sidebar->callsign().trimmed().toUpper();
    if (call.isEmpty()) {
        QMessageBox::information(this, "Log QSO",
            "Enter a callsign before logging.");
        return;
    }

    LogEntry e;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    e.date     = now.date();
    e.timeOn   = now.time();
    e.timeOff  = now.time();
    e.callsign = call;
    e.name     = m_sidebar->name().trimmed();
    e.qth      = m_sidebar->qth().trimmed();
    e.grid     = m_sidebar->grid().trimmed();
    if (e.grid.isEmpty())
        e.grid = QSettings("KC3SMW", "neodigi").value("station/grid", "").toString();

    // RST: use sidebar value, default to "59" if empty
    QString rst = m_sidebar->rst().trimmed();
    e.rstSent = rst.isEmpty() ? "59" : rst;
    e.rstRcvd = rst.isEmpty() ? "59" : rst;

    // Frequency and band from current fldigi state
    e.freq = m_client->getRigFrequency();
    e.band = LogEntry::freqToBand(e.freq);
    if (e.band.isEmpty() && e.freq > 0)
        e.band = QString::number(e.freq / 1e6, 'f', 3) + " MHz";

    // Mode from poll state
    e.mode = m_lastModem;

    // Operator from station info
    QSettings s("KC3SMW", "neodigi");
    e.opCall = s.value("station/callsign", "").toString();
    e.grid   = s.value("station/grid", "").toString();
    e.rig    = s.value("station/rig", "").toString();
    e.antenna = s.value("station/antenna", "").toString();

    // SNR from last poll
    e.snr = static_cast<int>(m_client->getQuality() * 30.0);

    // Park from settings
    e.park = s.value("station/park", "").toString();

    // Power from settings
    e.power = s.value("station/power", "").toString();

    m_logbook->addEntry(e);
    m_logbook->save();
    refreshLogTable();

    qInfo("[Logbook] QSO logged: %s %s %s %s",
          e.callsign.toUtf8().constData(),
          e.mode.toUtf8().constData(),
          e.band.toUtf8().constData(),
          e.date.toString(Qt::ISODate).toUtf8().constData());
}

void MainWindow::onClearQso()
{
    // Sidebar clears its own fields on Clear button press.
    // This hook is for any additional cleanup if needed.
}

// ---------------------------------------------------------------------------
// Logbook table
// ---------------------------------------------------------------------------

void MainWindow::refreshLogTable()
{
    const auto& entries = m_logbook->entries();
    m_logTable->setRowCount(entries.size());
    m_logTable->setAlternatingRowColors(true);

    const QStringList hdr = {
        "Date", "Time", "Call", "Band", "Freq", "Mode",
        "RST S", "RST R", "Name", "QTH", "Actions", "Notes"
    };
    m_logTable->setHorizontalHeaderLabels(hdr);

    for (int i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        auto item = [](const QString& t, bool editable = false) -> QTableWidgetItem* {
            auto* it = new QTableWidgetItem(t);
            it->setFlags(editable
                ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)
                : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
            return it;
        };
        m_logTable->setItem(i, 0, item(e.date.toString("yyyy-MM-dd")));
        m_logTable->setItem(i, 1, item(e.timeOn.toString("HHmm")));
        m_logTable->setItem(i, 2, item(e.callsign));
        m_logTable->setItem(i, 3, item(e.band));
        m_logTable->setItem(i, 4, item(e.freq > 0
            ? QString::number(e.freq, 'f', 1) : ""));
        m_logTable->setItem(i, 5, item(e.mode));
        m_logTable->setItem(i, 6, item(e.rstSent));
        m_logTable->setItem(i, 7, item(e.rstRcvd));
        m_logTable->setItem(i, 8, item(e.name));
        m_logTable->setItem(i, 9, item(e.qth));
        m_logTable->setItem(i, 11, item(e.notes, true));

        // Inline action buttons: ✎ Edit / ✕ Del — centered in row
        auto* actWidget = new QWidget;
        auto* actRow    = new QHBoxLayout(actWidget);
        actRow->setContentsMargins(2, 1, 2, 1);
        actRow->setSpacing(3);

        auto* editBtn = new QPushButton(QString::fromUtf8("\u270E"));
        editBtn->setFixedSize(28, 22);
        editBtn->setToolTip("Edit this log entry");

        auto* delBtn = new QPushButton(QString::fromUtf8("\u2715"));
        delBtn->setFixedSize(28, 22);
        delBtn->setToolTip("Delete this log entry");

        connect(editBtn, &QPushButton::clicked, this, [this, i]() {
            editLogEntry(i);
        });
        connect(delBtn, &QPushButton::clicked, this, [this, i]() {
            deleteLogEntry(i);
        });

        actRow->addWidget(editBtn);
        actRow->addWidget(delBtn);
        m_logTable->setCellWidget(i, 10, actWidget);
    }

    // Update tab text with QSO count
    const int n = entries.size();
    if (m_tabs) {
        const int idx = m_tabs->indexOf(m_logbookTab);
        if (idx >= 0)
            m_tabs->setTabText(idx, n > 0
                ? QString("Logbook (%1)").arg(n)
                : "Logbook");
    }
}

void MainWindow::exportAdif()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Logbook to ADIF",
        QDir::homePath() + "/neodigi_log.adi",
        "ADIF Files (*.adi *.adif);;All Files (*)");
    if (path.isEmpty()) return;

    if (m_logbook->exportAdifToFile(path)) {
        QMessageBox::information(this, "Export",
            QString("Exported %1 QSO%2 to:\n%3")
                .arg(m_logbook->entries().size())
                .arg(m_logbook->entries().size() == 1 ? "" : "s")
                .arg(path));
    } else {
        QMessageBox::warning(this, "Export Error",
            "Could not write to:\n" + path);
    }
}

void MainWindow::clearLogbook()
{
    if (m_logbook->entries().isEmpty()) return;

    auto ret = QMessageBox::question(this, "Clear Logbook",
        "Permanently delete all log entries?\n\n"
        "This cannot be undone. Export to ADIF first if needed.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    m_logbook->clearAll();
    m_logbook->save();
    refreshLogTable();
    qInfo("[Logbook] All entries cleared");
}

// ---------------------------------------------------------------------------
// Log entry edit / delete
// ---------------------------------------------------------------------------

void MainWindow::editLogEntry(int index)
{
    const auto& entries = m_logbook->entries();
    if (index < 0 || index >= entries.size()) return;

    LogEntry e = entries[index];
    if (!showLogEntryDialog(e, false))
        return;

    m_logbook->replaceEntry(index, e);
    m_logbook->save();
    refreshLogTable();

    qInfo("[Logbook] Entry %d updated", index);
}

void MainWindow::deleteLogEntry(int index)
{
    const auto& entries = m_logbook->entries();
    if (index < 0 || index >= entries.size()) return;

    const QString call = entries[index].callsign;
    auto ret = QMessageBox::question(this, "Delete Entry",
        QString("Delete log entry for %1 on %2?")
            .arg(call)
            .arg(entries[index].date.toString("yyyy-MM-dd")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    m_logbook->removeEntry(index);
    m_logbook->save();
    refreshLogTable();
    qInfo("[Logbook] Entry %d (%s) deleted", index, call.toUtf8().constData());
}

// ---------------------------------------------------------------------------
// Log entry editor dialog
// ---------------------------------------------------------------------------

bool MainWindow::showLogEntryDialog(LogEntry& entry, bool isNew)
{
    QDialog dlg(this);
    dlg.setWindowTitle(isNew ? "New Log Entry" : "Edit Log Entry");
    dlg.setMinimumWidth(420);

    auto* form = new QFormLayout(&dlg);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* dateEdit = new QLineEdit(entry.date.toString("yyyy-MM-dd"));
    auto* timeOnEdit = new QLineEdit(entry.timeOn.toString("HHmm"));
    auto* timeOffEdit = new QLineEdit(entry.timeOff.toString("HHmm"));
    auto* callEdit = new QLineEdit(entry.callsign);
    auto* freqEdit = new QLineEdit(entry.freq > 0 ? QString::number(entry.freq, 'f', 1) : "");
    auto* bandEdit = new QLineEdit(entry.band);
    auto* modeEdit = new QLineEdit(entry.mode);
    auto* rstSEdit = new QLineEdit(entry.rstSent);
    auto* rREdit   = new QLineEdit(entry.rstRcvd);
    auto* nameEdit = new QLineEdit(entry.name);
    auto* qthEdit  = new QLineEdit(entry.qth);
    auto* gridEdit = new QLineEdit(entry.grid);
    auto* snrEdit  = new QLineEdit(entry.snr > 0 ? QString::number(entry.snr) : "");
    auto* notesEdit = new QLineEdit(entry.notes);
    auto* parkEdit  = new QLineEdit(entry.park);
    auto* powerEdit = new QLineEdit(entry.power);
    auto* rigEdit   = new QLineEdit(entry.rig);
    auto* antEdit   = new QLineEdit(entry.antenna);

    form->addRow("Date (UTC):",        dateEdit);
    form->addRow("Time On (UTC):",     timeOnEdit);
    form->addRow("Time Off (UTC):",    timeOffEdit);
    form->addRow("Callsign:",          callEdit);
    form->addRow("Freq (Hz):",         freqEdit);
    form->addRow("Band:",              bandEdit);
    form->addRow("Mode:",              modeEdit);
    form->addRow("RST Sent:",          rstSEdit);
    form->addRow("RST Received:",      rREdit);
    form->addRow("Name:",              nameEdit);
    form->addRow("QTH:",               qthEdit);
    form->addRow("Grid:",              gridEdit);
    form->addRow("SNR:",               snrEdit);
    form->addRow("POTA Park:",         parkEdit);
    form->addRow("Power (W):",         powerEdit);
    form->addRow("Rig:",               rigEdit);
    form->addRow("Antenna:",           antEdit);
    form->addRow("Notes:",             notesEdit);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    entry.date     = QDate::fromString(dateEdit->text().trimmed(), "yyyy-MM-dd");
    entry.timeOn   = QTime::fromString(timeOnEdit->text().trimmed(), "HHmm");
    entry.timeOff  = QTime::fromString(timeOffEdit->text().trimmed(), "HHmm");
    entry.callsign = callEdit->text().trimmed().toUpper();
    entry.band     = bandEdit->text().trimmed();
    entry.freq     = freqEdit->text().trimmed().toDouble();
    entry.mode     = modeEdit->text().trimmed();
    entry.rstSent  = rstSEdit->text().trimmed();
    entry.rstRcvd  = rREdit->text().trimmed();
    entry.name     = nameEdit->text().trimmed();
    entry.qth      = qthEdit->text().trimmed();
    entry.grid     = gridEdit->text().trimmed();
    entry.snr      = snrEdit->text().trimmed().toInt();
    entry.notes    = notesEdit->text().trimmed();
    entry.park     = parkEdit->text().trimmed();
    entry.power    = powerEdit->text().trimmed();
    entry.rig      = rigEdit->text().trimmed();
    entry.antenna  = antEdit->text().trimmed();

    return true;
}

// ---------------------------------------------------------------------------
// fldigi path dialog
// ---------------------------------------------------------------------------

static QString fldigiPathFile()
{
    return QDir::homePath() + "/.neodigi-fldigi/fldigi_path.conf";
}

static QString readFldigiPath()
{
    QFile f(fldigiPathFile());
    if (!f.open(QFile::ReadOnly)) return {};
    const QString path = QString::fromUtf8(f.readAll()).trimmed();
    if (QFile::exists(path)) return path;
    return {};
}

static void writeFldigiPath(const QString& path)
{
    QDir().mkpath(QDir::homePath() + "/.neodigi-fldigi");
    QFile f(fldigiPathFile());
    if (f.open(QFile::WriteOnly | QFile::Truncate))
        f.write(path.toUtf8());
}

void MainWindow::showFldigiPathDialog()
{
    const QString current = readFldigiPath();

    QDialog dlg(this);
    dlg.setWindowTitle("fldigi Path");
    dlg.setMinimumWidth(500);

    auto* form = new QFormLayout(&dlg);
    auto* warn = new QLabel(
        "Path to the fldigi executable. This is used by the launch script\n"
        "to start the fldigi modem engine before the neodigi UI.");
    warn->setWordWrap(true);
    warn->setObjectName("dimLabel");
    form->addRow(warn);

    auto* pathEdit = new QLineEdit(current);
    pathEdit->setPlaceholderText("/usr/bin/fldigi");
    auto* browseBtn = new QPushButton("Browse…");

    auto* row = new QHBoxLayout;
    row->addWidget(pathEdit, 1);
    row->addWidget(browseBtn);
    form->addRow("fldigi binary:", row);

    // Validate path
    auto* validLabel = new QLabel;
    validLabel->setObjectName("dimLabel");
    form->addRow("", validLabel);

    auto updateValidation = [&]() {
        const QString p = pathEdit->text().trimmed();
        if (p.isEmpty()) {
            validLabel->setText("(will auto-detect from PATH)");
            validLabel->setStyleSheet("color: #6a6a6a;");
        } else if (QFile::exists(p)) {
            validLabel->setText("✓ File exists");
            validLabel->setStyleSheet("color: #4ec9b0;");
        } else {
            validLabel->setText("✗ File not found");
            validLabel->setStyleSheet("color: #f44747;");
        }
    };
    connect(pathEdit, &QLineEdit::textChanged, this, [updateValidation]() { updateValidation(); });
    connect(browseBtn, &QPushButton::clicked, this, [&]() {
        const QString p = QFileDialog::getOpenFileName(&dlg,
            "Select fldigi executable", "/usr/bin", "fldigi (fldigi);;All Files (*)");
        if (!p.isEmpty()) {
            pathEdit->setText(p);
            updateValidation();
        }
    });
    updateValidation();

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        const QString path = pathEdit->text().trimmed();
        if (path.isEmpty()) {
            // Clear saved path — launch script will fall back to PATH lookup
            if (QFile::exists(fldigiPathFile()))
                QFile::remove(fldigiPathFile());
        } else {
            writeFldigiPath(path);
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_isLiveTx) {
        m_liveTxFlushTimer->stop();
        if (!m_liveTxPending.isEmpty())
            m_client->addTx(m_liveTxPending);
        m_client->addTx(" ^r");
        m_isLiveTx = false;
    }
    saveFldigiState();
    QSettings s("KC3SMW", "neodigi");
    s.setValue("ui/geometry",      saveGeometry());
    s.setValue("ui/splitterState", m_splitter->saveState());
    if (m_tabs)
        s.setValue("ui/tabIndex", m_tabs->currentIndex());
    m_pollTimer->stop();
    m_txPulseTimer->stop();
    event->accept();
}
