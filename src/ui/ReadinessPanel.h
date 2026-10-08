#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QVBoxLayout;
class ServiceSupervisor;

class ReadinessPanel : public QDialog
{
    Q_OBJECT

public:
    explicit ReadinessPanel(ServiceSupervisor &supervisor, QWidget *parent = nullptr);

private slots:
    void onRefreshClicked();
    void onStatusesChanged();

private:
    void rebuildRows();

    ServiceSupervisor &m_supervisor;
    QVBoxLayout *m_rowsLayout = nullptr;
    QLabel *m_title = nullptr;
};
