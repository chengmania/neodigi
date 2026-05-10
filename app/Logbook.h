#pragma once

#include <QString>
#include <QDate>
#include <QTime>
#include <QList>
#include <QJsonObject>

struct LogEntry {
    QDate   date;
    QTime   timeOn;
    QTime   timeOff;
    QString callsign;
    QString band;
    double  freq = 0.0;
    QString mode;
    QString rstSent;
    QString rstRcvd;
    QString name;
    QString qth;
    QString grid;
    QString opCall;
    QString notes;
    QString park;
    QString power;
    QString rig;
    QString antenna;
    int     snr = 0;

    QJsonObject toJson() const;
    static LogEntry fromJson(const QJsonObject& obj);

    static QString freqToBand(double freqHz);
    QString adifLine() const;
};

class Logbook {
public:
    explicit Logbook(const QString& filePath = QString());

    void addEntry(const LogEntry& entry);
    void replaceEntry(int index, const LogEntry& entry);
    void removeEntry(int index);
    void clearAll();
    void replaceAll(const QList<LogEntry>& entries);

    const QList<LogEntry>& entries() const { return m_entries; }
    QList<LogEntry>& entries() { return m_entries; }

    void load(const QString& filePath);
    void save() const;
    void saveAs(const QString& filePath) const;

    QString exportAdif() const;
    bool exportAdifToFile(const QString& filePath) const;

    bool isDirty() const { return m_dirty; }
    void setDirty(bool d) { m_dirty = d; }

private:
    QList<LogEntry> m_entries;
    QString         m_filePath;
    bool            m_dirty = false;
};
