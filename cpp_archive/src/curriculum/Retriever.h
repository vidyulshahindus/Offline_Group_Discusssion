#pragma once

#include "UnitPack.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct RetrievalHit {
    QString text;
    double score = 0.0;
};

class Retriever
{
public:
    void buildFromUnit(const UnitPack &pack, int topicIndex);

    QVector<RetrievalHit> query(const QString &text, int topK = 2) const;
    QString notesForTurn(const QString &queryText, int maxWords = 160, double minScore = 1.0) const;

    bool isEmpty() const { return m_docs.isEmpty(); }
    int documentCount() const { return m_docs.size(); }

private:
    static QStringList tokenize(const QString &text);
    static bool scriptsMismatch(const QString &a, const QString &b);

    struct Doc {
        QString text;
        QStringList tokens;
        int length = 0;
    };

    QVector<Doc> m_docs;
    double m_avgDocLen = 0.0;
    QHash<QString, int> m_df;
};
