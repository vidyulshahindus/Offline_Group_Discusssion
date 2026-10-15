#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct LlmMessage {
    QString role;
    QString content;
};

struct LlmMetrics {
    qint64 promptEvalCount = 0;
    qint64 promptEvalCachedCount = -1;
    qint64 promptEvalNs = 0;
    qint64 evalCount = 0;
    qint64 evalNs = 0;
    qint64 loadNs = 0;
    qint64 totalNs = 0;
    qint64 msToFirstToken = -1;
    QString doneReason;

    qint64 promptEvalNewCount() const
    {
        if (promptEvalCachedCount >= 0 && promptEvalCount >= promptEvalCachedCount) {
            return promptEvalCount - promptEvalCachedCount;
        }
        return promptEvalCount;
    }
};

struct LlmRequest {
    QString model;
    QVector<LlmMessage> messages;
    int numPredict = 200;
    double temperature = 0.6;
    double topP = 0.9;
    int topK = 40;
    double repeatPenalty = 1.1;
    QStringList stop;
    QJsonObject jsonSchema;
    QString purpose; // turn | followup | warmup | summary | prep | bench
    int turnId = -1;
};

class ILlmBackend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void start(const LlmRequest &req) = 0;
    virtual void abort() = 0;
    virtual bool isBusy() const = 0;
    virtual void probe() = 0;
    virtual void showModel(const QString &model) = 0;
    virtual void preload(const QString &model) = 0;

signals:
    void token(int turnId, const QString &text);
    void finished(int turnId, const QString &fullText, const LlmMetrics &metrics);
    void failed(int turnId, const QString &error, bool recoverable);
    void probed(bool ok, const QString &version, const QStringList &models);
    void modelShown(const QString &model, const QStringList &capabilities, qint64 sizeBytes);
    void preloaded(const QString &model, bool ok);
};
