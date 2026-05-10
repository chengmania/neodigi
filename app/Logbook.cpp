#include "Logbook.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QDateTime>

// ---------------------------------------------------------------------------
// Band detection from frequency
// ---------------------------------------------------------------------------

QString LogEntry::freqToBand(double freqHz)
{
    const double mhz = freqHz / 1e6;
    if (mhz >= 1.8    && mhz < 2.0)     return "160m";
    if (mhz >= 3.5    && mhz < 4.0)     return "80m";
    if (mhz >= 5.3    && mhz < 5.4)     return "60m";
    if (mhz >= 7.0    && mhz < 7.3)     return "40m";
    if (mhz >= 10.1   && mhz < 10.15)   return "30m";
    if (mhz >= 14.0   && mhz < 14.35)   return "20m";
    if (mhz >= 18.068 && mhz < 18.168)  return "17m";
    if (mhz >= 21.0   && mhz < 21.45)   return "15m";
    if (mhz >= 24.89  && mhz < 24.99)   return "12m";
    if (mhz >= 28.0   && mhz < 29.7)    return "10m";
    if (mhz >= 50     && mhz < 54)      return "6m";
    if (mhz >= 144    && mhz < 148)     return "2m";
    if (mhz >= 222    && mhz < 225)     return "1.25m";
    if (mhz >= 420    && mhz < 450)     return "70cm";
    if (mhz >= 902    && mhz < 928)     return "33cm";
    if (mhz >= 1240   && mhz < 1300)    return "23cm";
    return QString();
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

QJsonObject LogEntry::toJson() const
{
    QJsonObject o;
    o["date"]     = date.toString(Qt::ISODate);
    o["timeOn"]   = timeOn.toString("HHmm");
    o["timeOff"]  = timeOff.toString("HHmm");
    o["callsign"] = callsign;
    o["band"]     = band;
    o["freq"]     = freq;
    o["mode"]     = mode;
    o["rstSent"]  = rstSent;
    o["rstRcvd"]  = rstRcvd;
    o["name"]     = name;
    o["qth"]      = qth;
    o["grid"]     = grid;
    o["opCall"]   = opCall;
    o["notes"]    = notes;
    o["park"]     = park;
    o["power"]    = power;
    o["rig"]      = rig;
    o["antenna"]  = antenna;
    o["snr"]      = snr;
    return o;
}

LogEntry LogEntry::fromJson(const QJsonObject& o)
{
    LogEntry e;
    e.date     = QDate::fromString(o["date"].toString(), Qt::ISODate);
    e.timeOn   = QTime::fromString(o["timeOn"].toString(), "HHmm");
    e.timeOff  = QTime::fromString(o["timeOff"].toString(), "HHmm");
    e.callsign = o["callsign"].toString();
    e.band     = o["band"].toString();
    e.freq     = o["freq"].toDouble();
    e.mode     = o["mode"].toString();
    e.rstSent  = o["rstSent"].toString();
    e.rstRcvd  = o["rstRcvd"].toString();
    e.name     = o["name"].toString();
    e.qth      = o["qth"].toString();
    e.grid     = o["grid"].toString();
    e.opCall   = o["opCall"].toString();
    e.notes    = o["notes"].toString();
    e.park     = o["park"].toString();
    e.power    = o["power"].toString();
    e.rig      = o["rig"].toString();
    e.antenna  = o["antenna"].toString();
    e.snr      = o["snr"].toInt();
    return e;
}

// ---------------------------------------------------------------------------
// ADIF export (one line per entry, including <EOR> separator)
// ---------------------------------------------------------------------------

static QString adifEscape(const QString& s)
{
    QString out = s;
    // ADIF does not require character escaping per se,
    // but we strip newlines to keep records clean.
    out.replace('\n', ' ');
    out.replace('\r', ' ');
    return out.trimmed();
}

QString LogEntry::adifLine() const
{
    QString out;

    auto f = [&](const char* tag, const QString& val) {
        if (!val.isEmpty()) {
            const QString v = adifEscape(val);
            out += QString("<%1:%2>%3 ").arg(tag).arg(v.size()).arg(v);
        }
    };

    auto fd = [&](const char* tag, double val, int prec) {
        if (val > 0) {
            const QString v = QString::number(val, 'f', prec);
            out += QString("<%1:%2>%3 ").arg(tag).arg(v.size()).arg(v);
        }
    };

    f("CALL",      callsign);
    f("BAND",      band);
    fd("FREQ",     freq, 3);
    f("MODE",      mode);
    f("RST_SENT",  rstSent);
    f("RST_RCVD",  rstRcvd);
    f("NAME",      name);
    f("QTH",       qth);
    f("GRIDSQUARE", grid);
    f("OPERATOR",  opCall);
    f("COMMENT",   notes);
    f("MY_POTA",   park);
    f("TX_PWR",    power);
    f("STATION_CALLSIGN", opCall);
    f("EQSL_QSL_SENT", "N");
    f("LOTW_QSL_SENT", "N");

    // Dates in ADIF format YYYYMMDD
    if (date.isValid()) {
        const QString ds = date.toString("yyyyMMdd");
        out += QString("<QSO_DATE:%1>%2 ").arg(ds.size()).arg(ds);
    }

    // Times in HHMMSS
    if (timeOn.isValid()) {
        const QString ts = timeOn.toString("HHmmss");
        out += QString("<TIME_ON:%1>%2 ").arg(ts.size()).arg(ts);
    }
    if (timeOff.isValid() && timeOff != timeOn) {
        const QString ts = timeOff.toString("HHmmss");
        out += QString("<TIME_OFF:%1>%2 ").arg(ts.size()).arg(ts);
    }

    out += "<EOR>";
    return out;
}

// ---------------------------------------------------------------------------
// Logbook
// ---------------------------------------------------------------------------

Logbook::Logbook(const QString& filePath)
    : m_filePath(filePath)
{
    if (!m_filePath.isEmpty())
        load(m_filePath);
}

void Logbook::addEntry(const LogEntry& entry)
{
    m_entries.append(entry);
    m_dirty = true;
}

void Logbook::replaceEntry(int index, const LogEntry& entry)
{
    if (index >= 0 && index < m_entries.size()) {
        m_entries[index] = entry;
        m_dirty = true;
    }
}

void Logbook::removeEntry(int index)
{
    if (index >= 0 && index < m_entries.size()) {
        m_entries.removeAt(index);
        m_dirty = true;
    }
}

void Logbook::clearAll()
{
    m_entries.clear();
    m_dirty = true;
}

void Logbook::replaceAll(const QList<LogEntry>& entries)
{
    m_entries = entries;
    m_dirty = true;
}

void Logbook::load(const QString& filePath)
{
    m_filePath = filePath;
    QFile f(m_filePath);
    if (!f.open(QFile::ReadOnly)) return;

    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    m_entries.clear();
    m_entries.reserve(arr.size());
    for (const auto& val : arr)
        m_entries.append(LogEntry::fromJson(val.toObject()));
    m_dirty = false;
}

void Logbook::save() const
{
    if (m_filePath.isEmpty()) return;
    saveAs(m_filePath);
}

void Logbook::saveAs(const QString& filePath) const
{
    const QString path = filePath.isEmpty() ? m_filePath : filePath;
    if (path.isEmpty()) return;

    QJsonArray arr;
    for (const auto& e : m_entries)
        arr.append(e.toJson());

    QFile f(path);
    if (f.open(QFile::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QString Logbook::exportAdif() const
{
    QString out;
    out += "Generated by neodigi v0.1.0\n";
    out += "<ADIF_VER:5>3.1.4\n";
    out += "<PROGRAMID:7>neodigi\n";
    out += "<PROGRAMVERSION:5>0.1.0\n";
    out += "<EOH>\n\n";
    for (const auto& e : m_entries) {
        out += e.adifLine() + "\n";
    }
    return out;
}

bool Logbook::exportAdifToFile(const QString& filePath) const
{
    QFile f(filePath);
    if (!f.open(QFile::WriteOnly | QFile::Text))
        return false;
    QTextStream out(&f);
    out << exportAdif();
    return true;
}
