#include "MetricsOverlay.h"

#include "Metrics.h"

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
    if (t.llm.promptEvalCount > 0 && t.estimatedNewPromptTokens > 0) {
        const double ratio = static_cast<double>(t.llm.promptEvalCount) / t.estimatedNewPromptTokens;
        cache = ratio <= 1.8 ? QStringLiteral("likely HIT") : QStringLiteral("likely MISS");
    }
    m_body->setText(
        QStringLiteral("Model: %1\n"
                       "Last turn #%2\n"
                       "First token: %3 ms\n"
                       "prompt_eval_count: %4 (est. new ~%5) → %6\n"
                       "eval_count: %7\n"
                       "load_duration: %8 ms\n"
                       "Rolling first-token p50: %9 ms | p95: %10 ms")
            .arg(t.model.isEmpty() ? QStringLiteral("—") : t.model)
            .arg(t.turnId)
            .arg(t.firstTokenMs)
            .arg(t.llm.promptEvalCount)
            .arg(t.estimatedNewPromptTokens)
            .arg(cache)
            .arg(t.llm.evalCount)
            .arg(t.llm.loadNs / 1000000)
            .arg(m_metrics.rollingP50FirstToken(), 0, 'f', 0)
            .arg(m_metrics.rollingP95FirstToken(), 0, 'f', 0));
}
