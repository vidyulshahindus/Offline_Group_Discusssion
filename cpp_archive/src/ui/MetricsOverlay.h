#pragma once

#include <QDialog>

class QLabel;
class Metrics;

class MetricsOverlay : public QDialog
{
    Q_OBJECT

public:
    explicit MetricsOverlay(Metrics &metrics, QWidget *parent = nullptr);

    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

private:
    Metrics &m_metrics;
    QLabel *m_body = nullptr;
};
