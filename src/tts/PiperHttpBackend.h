#pragma once

#include <QObject>

// HTTP backend (piper1-gpl) — wired in M11 packaging; M2 selects via config but CLI is default.
class PiperHttpBackend : public QObject
{
    Q_OBJECT

public:
    explicit PiperHttpBackend(QObject *parent = nullptr) : QObject(parent) {}
};
