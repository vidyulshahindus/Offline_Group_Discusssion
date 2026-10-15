#include "MetricsOverlay.h"

#include "Metrics.h"
#include "TurnTimestamps.h"

#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>

MetricsOverlay::MetricsOverlay(Metrics &metrics, QWidget *parent)
    : QDialog(parent)
    , m_metrics(metrics)
{
    setWindowTitle(QStringLiteral("Eagle GD — Metrics (F12)"));
    resize(480, 320);
    auto *layout = new QVBoxLayout(this);
    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_body);
    refresh();
}

void MetricsOverlay::showEvent(QShowEvent *event)
{
    refresh();
    QDialog::showEvent(event);
}

void MetricsOverlay::refresh()
{
    const TurnMetrics &t = m_metrics.lastTurn();
    QString cache = QStringLiteral("n/a");
    const qint64 promptNew = t.llm.promptEvalNewCount();
    if (promptNew > 0 && t.estimatedNewPromptTokens > 0) {
        const double ratio = static_cast<double>(promptNew) / t.estimatedNewPromptTokens;
        cache = ratio <= 1.8 ? QStringLiteral("likely HIT") : QStringLiteral("likely MISS");
    }
    QString promptLine;
    if (t.llm.promptEvalCachedCount >= 0) {
        promptLine = QStringLiteral("prompt_eval: %1 new + %2 cached = %3 total (est. new ~%4) → %5")
                         .arg(promptNew)
                         .arg(t.llm.promptEvalCachedCount)
                         .arg(t.llm.promptEvalCount)
                         .arg(t.estimatedNewPromptTokens)
                         .arg(cache);
    } else {
        promptLine = QStringLiteral("prompt_eval_count: %1 (est. new ~%2) → %3")
                         .arg(t.llm.promptEvalCount)
                         .arg(t.estimatedNewPromptTokens)
                         .arg(cache);
    }
    const TurnTimestamps &ts = t.timestamps;
    m_body->setText(
        QStringLiteral("Model: %1\n"
                       "Last turn #%2\n"
                       "T0→T1 (first audio): %3 ms | rolling p50 %4 / p95 %5\n"
                       "Timestamps (ms from T0): prompt %6 | sent %7 | 1st token %8 | "
                       "1st sentence %9 | 1st TTS %10 | LLM done %11 | speech done %12\n"
                       "First token (LLM): %13 ms\n"
                       "TTS synth (turn): first %14 ms | warm %15 ms\n"
                       "TTS worker: cold %16 ms | warm %17 ms (persistent piper)\n"
                       "%18\n"
                       "eval_count: %19 | load: %20 ms")
            .arg(t.model.isEmpty() ? QStringLiteral("—") : t.model)
            .arg(t.turnId)
            .arg(t.t0ToT1Ms)
            .arg(m_metrics.rollingP50T0ToT1(), 0, 'f', 0)
            .arg(m_metrics.rollingP95T0ToT1(), 0, 'f', 0)
            .arg(ts.promptBuiltMs)
            .arg(ts.requestSentMs)
            .arg(ts.firstTokenMs)
            .arg(ts.firstSentenceMs)
            .arg(ts.firstTtsDoneMs)
            .arg(ts.llmDoneMs)
            .arg(ts.speechDoneMs)
            .arg(t.firstTokenMs)
            .arg(t.ttsFirstSynthMs)
            .arg(t.ttsWarmSynthMs)
            .arg(t.ttsWorkerColdSynthMs)
            .arg(t.ttsWorkerWarmSynthMs)
            .arg(promptLine)
            .arg(t.llm.evalCount)
            .arg(t.llm.loadNs / 1000000));
}
