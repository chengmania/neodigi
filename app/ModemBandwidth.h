#pragma once
#include <QMap>
#include <QString>

static const QMap<QString, int> MODEM_BW = {
    {"BPSK31",  31},  {"BPSK63",  63},  {"BPSK125", 125},
    {"BPSK250", 250}, {"BPSK500", 500},
    {"QPSK31",  31},  {"QPSK63",  63},  {"QPSK125", 125},
    {"RTTY",   170},
    {"MFSK4",   44},  {"MFSK8",   88},  {"MFSK16",  316}, {"MFSK32", 634},
    {"MT63-500",  500}, {"MT63-1K",  1000}, {"MT63-2K",  2000},
    {"Olivia-4/125",   125}, {"Olivia-4/250",    250},
    {"Olivia-8/250",   250}, {"Olivia-8/500",    500},
    {"Olivia-16/500",  500}, {"Olivia-32/1000", 1000},
    {"Thor4",   44},  {"Thor8",   88},  {"Thor16",  176}, {"Thor22", 242},
    {"CW",     100},
    {"DominoEX4",   44}, {"DominoEX8",   88}, {"DominoEX16", 176},
    {"Contestia-4/125", 125}, {"Contestia-8/250", 250},
    {"WEFAX-576", 1500}, {"WEFAX-288", 750},
    {"FELDHELL",  350},
};
