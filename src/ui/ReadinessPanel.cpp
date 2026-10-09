#include "ReadinessPanel.h"

#include "Logging.h"
#include "ServiceSupervisor.h"
#include "TurnSpeechController.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
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

ReadinessPanel::ReadinessPanel(ServiceSupervisor &supervisor, TurnSpeechController *turnSpeech,
                               QWidget *parent)
    : QDialog(parent)
    , m_supervisor(supervisor)
    , m_turnSpeech(turnSpeech)
{
    setWindowTitle(QStringLiteral("Eagle GD — Readiness"));
    resize(580, 480);

    auto *root = new QVBoxLayout(this);

    m_title = new QLabel(QStringLiteral("System readiness"), this);
    m_title->setObjectName(QStringLiteral("readinessTitle"));
    QFont titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    m_title->setFont(titleFont);

    m_logLabel = new QLabel(this);
    m_logLabel->setObjectName(QStringLiteral("readinessDetail"));
    m_logLabel->setWordWrap(true);
    m_logLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    refreshLogLabel();

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *scrollHost = new QWidget(scroll);
    m_rowsLayout = new QVBoxLayout(scrollHost);
    m_rowsLayout->setSpacing(8);
    scroll->setWidget(scrollHost);

    m_testToneButton = new QPushButton(QStringLiteral("Test tone (440 Hz, 1 s)"), this);
    m_testToneButton->setEnabled(m_turnSpeech != nullptr);
    connect(m_testToneButton, &QPushButton::clicked, this, &ReadinessPanel::onTestToneClicked);

    m_testVoiceButton = new QPushButton(QStringLiteral("Test voice (English + Hindi)"), this);
    m_testVoiceButton->setEnabled(m_turnSpeech != nullptr);
    connect(m_testVoiceButton, &QPushButton::clicked, this, &ReadinessPanel::onTestVoiceClicked);

    if (m_turnSpeech) {
        connect(m_turnSpeech, &TurnSpeechController::readinessVoiceTestFinished, this,
                &ReadinessPanel::onVoiceTestFinished);
        connect(m_turnSpeech, &TurnSpeechController::testToneFinished, this,
                &ReadinessPanel::onTestToneFinished);
    }

    m_openLogButton = new QPushButton(QStringLiteral("Open log folder"), this);
    connect(m_openLogButton, &QPushButton::clicked, this, &ReadinessPanel::onOpenLogFolderClicked);

    auto *refresh = new QPushButton(QStringLiteral("Refresh checks"), this);
    connect(refresh, &QPushButton::clicked, this, &ReadinessPanel::onRefreshClicked);

    auto *close = new QPushButton(QStringLiteral("Continue to Eagle GD"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    root->addWidget(m_title);
    root->addWidget(m_logLabel);
    root->addWidget(scroll, 1);
    root->addWidget(m_testToneButton);
    root->addWidget(m_testVoiceButton);
    root->addWidget(m_openLogButton);
    root->addWidget(refresh);
    root->addWidget(close);

    connect(&m_supervisor, &ServiceSupervisor::statusesChanged, this, &ReadinessPanel::onStatusesChanged);
    rebuildRows();
}

void ReadinessPanel::refreshLogLabel()
{
    const QString path = EagleLog::filePath();
    if (path.isEmpty()) {
        m_logLabel->setText(QStringLiteral("Log file: (not created)"));
    } else {
        m_logLabel->setText(QStringLiteral("Log file: %1").arg(QDir::toNativeSeparators(path)));
    }
}

void ReadinessPanel::onTestToneClicked()
{
    if (!m_turnSpeech) {
        return;
    }
    m_testToneButton->setEnabled(false);
    m_testToneButton->setText(QStringLiteral("Playing test tone…"));
    m_turnSpeech->playTestTone();
}

void ReadinessPanel::onTestToneFinished(bool ok, const QString &detail)
{
    m_testToneButton->setEnabled(m_turnSpeech != nullptr);
    if (ok) {
        m_testToneButton->setText(QStringLiteral("Test tone (440 Hz, 1 s) — played"));
    } else {
        m_testToneButton->setText(
            QStringLiteral("Test tone failed — %1").arg(detail.left(80)));
    }
}

void ReadinessPanel::onOpenLogFolderClicked()
{
    refreshLogLabel();
    QString dir = EagleLog::directory();
    if (dir.isEmpty() && !EagleLog::filePath().isEmpty()) {
        dir = QFileInfo(EagleLog::filePath()).absolutePath();
    }
    if (dir.isEmpty()) {
        m_logLabel->setText(QStringLiteral("Log file: (not created)"));
        return;
    }
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void ReadinessPanel::onTestVoiceClicked()
{
    if (!m_turnSpeech) {
        return;
    }
    m_testVoiceButton->setEnabled(false);
    m_testVoiceButton->setText(QStringLiteral("Testing voice…"));
    m_turnSpeech->playReadinessVoiceTest();
}

void ReadinessPanel::onVoiceTestFinished(bool ok, const QString &detail)
{
    m_testVoiceButton->setEnabled(m_turnSpeech != nullptr);
    if (ok) {
        m_testVoiceButton->setText(QStringLiteral("Test voice (English + Hindi) — OK"));
    } else {
        m_testVoiceButton->setText(
            QStringLiteral("Test voice failed — %1").arg(detail.left(80)));
    }
}

void ReadinessPanel::onRefreshClicked()
{
    m_supervisor.refreshAll();
    refreshLogLabel();
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
