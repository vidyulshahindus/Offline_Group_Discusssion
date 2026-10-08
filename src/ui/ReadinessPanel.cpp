#include "ReadinessPanel.h"

#include "ServiceSupervisor.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QString healthText(ServiceHealth health)
{
    switch (health) {
    case ServiceHealth::Ok:
        return QStringLiteral("OK");
    case ServiceHealth::Warning:
        return QStringLiteral("Warning");
    case ServiceHealth::Error:
        return QStringLiteral("Error");
    case ServiceHealth::Checking:
        return QStringLiteral("Checking…");
    case ServiceHealth::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

} // namespace

ReadinessPanel::ReadinessPanel(ServiceSupervisor &supervisor, QWidget *parent)
    : QDialog(parent)
    , m_supervisor(supervisor)
{
    setWindowTitle(QStringLiteral("Eagle GD — Readiness"));
    resize(560, 420);

    auto *root = new QVBoxLayout(this);

    m_title = new QLabel(QStringLiteral("System readiness"), this);
    m_title->setObjectName(QStringLiteral("readinessTitle"));
    QFont titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    m_title->setFont(titleFont);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *scrollHost = new QWidget(scroll);
    m_rowsLayout = new QVBoxLayout(scrollHost);
    m_rowsLayout->setSpacing(8);
    scroll->setWidget(scrollHost);

    auto *refresh = new QPushButton(QStringLiteral("Refresh checks"), this);
    connect(refresh, &QPushButton::clicked, this, &ReadinessPanel::onRefreshClicked);

    auto *close = new QPushButton(QStringLiteral("Continue to Eagle GD"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    root->addWidget(m_title);
    root->addWidget(scroll, 1);
    root->addWidget(refresh);
    root->addWidget(close);

    connect(&m_supervisor, &ServiceSupervisor::statusesChanged, this, &ReadinessPanel::onStatusesChanged);
    rebuildRows();
}

void ReadinessPanel::onRefreshClicked()
{
    m_supervisor.refreshAll();
}

void ReadinessPanel::onStatusesChanged()
{
    rebuildRows();
}

void ReadinessPanel::rebuildRows()
{
    while (m_rowsLayout->count() > 0) {
        QLayoutItem *item = m_rowsLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    for (const ServiceStatus &status : m_supervisor.statuses()) {
        auto *row = new QFrame(this);
        row->setFrameShape(QFrame::StyledPanel);
        auto *layout = new QVBoxLayout(row);

        auto *header = new QHBoxLayout();
        auto *name = new QLabel(status.label, row);
        name->setObjectName(QStringLiteral("readinessRowName"));
        auto *badge = new QLabel(healthText(status.health), row);
        badge->setObjectName(QStringLiteral("readinessBadge"));
        header->addWidget(name, 1);
        header->addWidget(badge);

        auto *detail = new QLabel(status.detail, row);
        detail->setWordWrap(true);
        detail->setObjectName(QStringLiteral("readinessDetail"));

        layout->addLayout(header);
        layout->addWidget(detail);

        if (!status.fixHint.isEmpty() && status.health != ServiceHealth::Ok) {
            auto *hint = new QLabel(QStringLiteral("Fix: %1").arg(status.fixHint), row);
            hint->setWordWrap(true);
            hint->setObjectName(QStringLiteral("readinessHint"));
            layout->addWidget(hint);
        }

        m_rowsLayout->addWidget(row);
    }
    m_rowsLayout->addStretch(1);
}
