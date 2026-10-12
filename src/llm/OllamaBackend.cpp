#include "OllamaBackend.h"

#include "AppConfig.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

Q_LOGGING_CATEGORY(lcOllama, "eagle.ollama")

namespace {

QString baseUrlTrimmed(const AppConfig &config)
{
    QString url = config.llmBaseUrl().trimmed();
    if (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

} // namespace

OllamaBackend::OllamaBackend(const AppConfig &config, QObject *parent)
    : ILlmBackend(parent)
    , m_config(config)
    , m_network(new QNetworkAccessManager(this))
    , m_firstTokenTimer(new QTimer(this))
    , m_overallTimer(new QTimer(this))
{
    m_loadOptions.numCtx = config.llmNumCtx();
    m_loadOptions.numThread = config.llmNumThread();
    m_loadOptions.numBatch = config.llmNumBatch();

    m_firstTokenTimer->setSingleShot(true);
    m_firstTokenTimer->setInterval(12000);
    connect(m_firstTokenTimer, &QTimer::timeout, this, &OllamaBackend::onFirstTokenTimeout);

    m_overallTimer->setSingleShot(true);
    m_overallTimer->setInterval(config.llmRequestTimeoutMs());
    connect(m_overallTimer, &QTimer::timeout, this, &OllamaBackend::onOverallTimeout);

    m_warmScheduleTimer = new QTimer(this);
    m_warmScheduleTimer->setSingleShot(true);
    connect(m_warmScheduleTimer, &QTimer::timeout, this, [this]() {
        if (m_hasCoalescedWarm && !m_auxReply && !m_inStartupSequence) {
            enqueueCoalescedWarmIfNeeded();
        }
        pumpAuxQueue();
    });
}

OllamaBackend::~OllamaBackend()
{
    abort();
}

bool OllamaBackend::isBusy() const
{
    return m_streamReply != nullptr || m_auxReply != nullptr;
}

void OllamaBackend::safeAbortStream()
{
    QNetworkReply *reply = m_streamReply;
    if (!reply) {
        return;
    }
    m_streamReply = nullptr;
    reply->disconnect(this);
    reply->abort();
    m_firstTokenTimer->stop();
    m_overallTimer->stop();
    m_lineBuffer.clear();
    m_streamPurpose.clear();
    reply->deleteLater();
}

void OllamaBackend::abortStreamForThinkingRetry()
{
    QNetworkReply *reply = m_streamReply;
    if (!reply) {
        return;
    }
    m_streamReply = nullptr;
    m_firstTokenTimer->stop();
    m_overallTimer->stop();
    reply->abort();
}

void OllamaBackend::safeAbortAux()
{
    QNetworkReply *reply = m_auxReply;
    if (!reply) {
        return;
    }
    m_auxReply = nullptr;
    m_auxPurpose.clear();
    m_hasActiveAuxJob = false;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void OllamaBackend::abortActiveTurn()
{
    safeAbortStream();
}

void OllamaBackend::abort()
{
    safeAbortStream();
    safeAbortAux();
    m_auxQueue.clear();
    m_hasCoalescedWarm = false;
    m_startupAwaitingWarm = false;
    m_inStartupSequence = false;
    m_startupStep = StartupStep::Idle;
}

bool OllamaBackend::modelHasThinking(const QString &model) const
{
    return m_modelInfo.value(model).capabilities.contains(QStringLiteral("thinking"));
}

bool OllamaBackend::shouldSendThinkFalse(const QString &model) const
{
    if (m_modelsRequireThinkFalse.contains(model)) {
        return true;
    }
    if (modelHasThinking(model)) {
        return true;
    }
    static const QRegularExpression thinkingName(
        QStringLiteral("eagle-clm|qwen3"),
        QRegularExpression::CaseInsensitiveOption);
    return thinkingName.match(model).hasMatch();
}

void OllamaBackend::logChatRequest(const QString &purpose, const QJsonObject &body) const
{
    qCInfo(lcOllama) << "POST /api/chat" << purpose
                     << QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void OllamaBackend::logStreamLine(const QByteArray &line, qint64 msSincePost) const
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCInfo(lcOllama) << m_streamPurpose << "NDJSON parse error at" << msSincePost << "ms:"
                         << line.left(200);
        return;
    }
    const QJsonObject obj = doc.object();
    const bool done = obj.value(QStringLiteral("done")).toBool();
    const QString doneReason = obj.value(QStringLiteral("done_reason")).toString();
    const QJsonObject message = obj.value(QStringLiteral("message")).toObject();
    const int contentLen = message.value(QStringLiteral("content")).toString().size();
    const int thinkingLen = message.value(QStringLiteral("thinking")).toString().size();
    qCInfo(lcOllama) << m_streamPurpose << "line at" << msSincePost << "ms done=" << done
                     << "done_reason=" << doneReason << "content_chars=" << contentLen
                     << "thinking_chars=" << thinkingLen;
}

QJsonObject OllamaBackend::buildOptions(const LlmRequest &req) const
{
    QJsonObject options;
    options.insert(QStringLiteral("num_ctx"), m_loadOptions.numCtx);
    if (m_loadOptions.numThread > 0) {
        options.insert(QStringLiteral("num_thread"), m_loadOptions.numThread);
    }
    options.insert(QStringLiteral("num_batch"), m_loadOptions.numBatch);
    options.insert(QStringLiteral("num_predict"), req.numPredict);
    options.insert(QStringLiteral("temperature"), req.temperature);
    options.insert(QStringLiteral("top_p"), req.topP);
    options.insert(QStringLiteral("top_k"), req.topK);
    options.insert(QStringLiteral("repeat_penalty"), req.repeatPenalty);
    if (!req.stop.isEmpty()) {
        options.insert(QStringLiteral("stop"), QJsonArray::fromStringList(req.stop));
    }
    return options;
}

QJsonObject OllamaBackend::buildChatBody(const QString &model, const QVector<LlmMessage> &messages,
                                         const LlmRequest &req, bool forWarmUp, bool stream) const
{
    QJsonArray msgArray;
    for (const LlmMessage &message : messages) {
        QJsonObject obj;
        obj.insert(QStringLiteral("role"), message.role);
        obj.insert(QStringLiteral("content"), message.content);
        msgArray.append(obj);
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), msgArray);
    body.insert(QStringLiteral("stream"), stream);
    body.insert(QStringLiteral("keep_alive"), m_config.llmKeepAlive());

    if (shouldSendThinkFalse(model)) {
        body.insert(QStringLiteral("think"), false);
    }

    LlmRequest effective = req;
    if (forWarmUp) {
        effective.numPredict = 2;
        effective.temperature = 0.1;
    }
    body.insert(QStringLiteral("options"), buildOptions(effective));

    if (!req.jsonSchema.isEmpty()) {
        body.insert(QStringLiteral("format"), req.jsonSchema);
    }
    return body;
}

void OllamaBackend::runStartupSequence(const QString &model, const QString &fallbackModel,
                                       const QString &systemPrompt)
{
    m_auxQueue.clear();
    m_hasCoalescedWarm = false;
    m_inStartupSequence = true;
    m_startupAwaitingWarm = true;
    m_startupPrimaryModel = model;
    m_startupFallbackModel = fallbackModel;
    m_startupSystemPrompt = systemPrompt;
    m_startupStep = StartupStep::ShowPrimary;

    m_auxQueue.enqueue(AuxJob{AuxJob::Kind::Show, model, {}});
    if (!fallbackModel.isEmpty() && fallbackModel != model) {
        m_auxQueue.enqueue(AuxJob{AuxJob::Kind::Show, fallbackModel, {}});
        m_startupStep = StartupStep::ShowFallback;
    }
    m_auxQueue.enqueue(AuxJob{AuxJob::Kind::Preload, model, {}});
    m_startupStep = StartupStep::Preload;
    pumpAuxQueue();
}

void OllamaBackend::scheduleWarmUp(const QString &model, const QString &systemPrompt)
{
    m_coalescedWarmModel = model;
    m_coalescedWarmPrompt = systemPrompt;
    m_hasCoalescedWarm = true;
    if (m_inStartupSequence || m_auxReply) {
        return;
    }
    m_warmScheduleTimer->start(0);
}

void OllamaBackend::enqueueCoalescedWarmIfNeeded()
{
    if (!m_hasCoalescedWarm) {
        return;
    }
    m_auxQueue.enqueue(
        AuxJob{AuxJob::Kind::WarmUp, m_coalescedWarmModel, m_coalescedWarmPrompt});
    m_hasCoalescedWarm = false;
}

void OllamaBackend::pumpAuxQueue()
{
    if (m_auxReply) {
        return;
    }

    if (m_auxQueue.isEmpty()) {
        if (m_hasCoalescedWarm && !m_inStartupSequence) {
            enqueueCoalescedWarmIfNeeded();
        }
        if (m_auxQueue.isEmpty()) {
            return;
        }
    }

    const AuxJob job = m_auxQueue.dequeue();
    startAuxJob(job);
}

void OllamaBackend::startAuxJob(const AuxJob &job)
{
    if (m_auxReply) {
        qCWarning(lcOllama) << "startAuxJob while aux in flight";
        return;
    }

    m_activeAuxJob = job;
    m_hasActiveAuxJob = true;

    if (job.kind == AuxJob::Kind::Show) {
        const QUrl url(baseUrlTrimmed(m_config) + QStringLiteral("/api/show"));
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QJsonObject body;
        body.insert(QStringLiteral("model"), job.model);
        m_auxPurpose = QStringLiteral("show:%1").arg(job.model);
        m_auxReply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(m_auxReply, &QNetworkReply::finished, this, &OllamaBackend::onAuxReplyFinished);
        return;
    }

    QJsonObject body;
    if (job.kind == AuxJob::Kind::Preload) {
        m_pendingPreloadModel = job.model;
        m_auxPurpose = QStringLiteral("preload");
        body.insert(QStringLiteral("model"), job.model);
        body.insert(QStringLiteral("messages"), QJsonArray());
        body.insert(QStringLiteral("stream"), false);
        body.insert(QStringLiteral("keep_alive"), m_config.llmKeepAlive());
    } else {
        m_auxPurpose = QStringLiteral("warmup");
        QVector<LlmMessage> messages;
        LlmMessage system;
        system.role = QStringLiteral("system");
        system.content = job.systemPrompt;
        messages.append(system);
        LlmMessage user;
        user.role = QStringLiteral("user");
        user.content = QStringLiteral("Reply with one word: Ready.");
        messages.append(user);

        LlmRequest req;
        req.model = job.model;
        req.messages = messages;
        req.purpose = QStringLiteral("warmup");
        req.numPredict = 2;
        req.temperature = 0.1;
        body = buildChatBody(job.model, messages, req, true, false);
    }

    logChatRequest(m_auxPurpose, body);
    const QUrl url(baseUrlTrimmed(m_config) + QStringLiteral("/api/chat"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_auxReply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_auxReply, &QNetworkReply::finished, this, &OllamaBackend::onAuxReplyFinished);
}

void OllamaBackend::completeAuxJob(const AuxJob &job, bool ok, const QByteArray &body)
{
    if (job.kind == AuxJob::Kind::Show) {
        if (ok) {
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            const QJsonObject obj = doc.object();
            const QString model = obj.value(QStringLiteral("model")).toString();
            QStringList caps;
            for (const QJsonValue &cap : obj.value(QStringLiteral("capabilities")).toArray()) {
                caps.append(cap.toString());
            }
            if (caps.isEmpty()) {
                const QJsonObject details = obj.value(QStringLiteral("details")).toObject();
                for (const QJsonValue &cap : details.value(QStringLiteral("capabilities")).toArray()) {
                    caps.append(cap.toString());
                }
            }
            if (caps.isEmpty() && shouldSendThinkFalse(model)) {
                caps.append(QStringLiteral("thinking"));
            }
            ModelInfo info;
            info.capabilities = caps;
            info.sizeBytes = obj.value(QStringLiteral("size")).toInteger();
            m_modelInfo.insert(model, info);
            emit modelShown(model, caps, info.sizeBytes);
        }
        return;
    }

    if (job.kind == AuxJob::Kind::Preload) {
        emit preloaded(m_pendingPreloadModel, ok);
        m_pendingPreloadModel.clear();
        if (m_startupAwaitingWarm) {
            m_startupAwaitingWarm = false;
            if (!m_hasCoalescedWarm) {
                m_coalescedWarmModel = m_startupPrimaryModel;
                m_coalescedWarmPrompt = m_startupSystemPrompt;
                m_hasCoalescedWarm = true;
            }
            enqueueCoalescedWarmIfNeeded();
        }
        return;
    }

    emit warmUpFinished(ok);
    if (m_inStartupSequence) {
        m_inStartupSequence = false;
        m_startupStep = StartupStep::Idle;
        emit startupSequenceFinished(ok);
    }
}

void OllamaBackend::onAuxReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_auxReply) {
        if (reply) {
            reply->deleteLater();
        }
        return;
    }

    m_auxReply = nullptr;
    const QString purpose = m_auxPurpose;
    m_auxPurpose.clear();
    const AuxJob job = m_activeAuxJob;
    m_hasActiveAuxJob = false;

    reply->disconnect(this);
    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    qCInfo(lcOllama) << purpose << "HTTP status=" << httpStatus << "ok=" << ok;

    completeAuxJob(job, ok, body);
    if (job.kind == AuxJob::Kind::WarmUp && m_hasCoalescedWarm && !m_inStartupSequence) {
        m_warmScheduleTimer->start(0);
    } else {
        pumpAuxQueue();
    }
}

void OllamaBackend::start(const LlmRequest &req)
{
    safeAbortStream();
    if (m_auxReply) {
        qCWarning(lcOllama) << "Turn start blocked: aux busy (" << m_auxPurpose << ")";
        emit failed(req.turnId, QStringLiteral("Eagle is still preparing the model — try again."),
                    true);
        return;
    }

    m_activeRequest = req;
    m_turnId = req.turnId;
    m_turnThinkingRetryUsed = false;
    m_pendingThinkFalseRetry = false;
    m_streamPurpose = QStringLiteral("turn:%1").arg(req.purpose);
    m_requestTimer.start();
    beginTurnStream();
}

void OllamaBackend::beginTurnStream()
{
    m_streamText.clear();
    m_lineBuffer.clear();
    m_gotFirstToken = false;
    m_streamCompleted = false;
    m_firstTokenMs = -1;
    m_loggedFirstStreamByte = false;
    m_streamPostTimer.start();

    m_currentRequestSentThinkFalse = shouldSendThinkFalse(m_activeRequest.model);
    const QJsonObject body =
        buildChatBody(m_activeRequest.model, m_activeRequest.messages, m_activeRequest, false, true);
    logChatRequest(m_streamPurpose, body);
    const QUrl url(baseUrlTrimmed(m_config) + QStringLiteral("/api/chat"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    m_streamReply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_streamReply, &QNetworkReply::readyRead, this, &OllamaBackend::onStreamReadyRead);
    connect(m_streamReply, &QNetworkReply::finished, this, &OllamaBackend::onStreamFinished);

    m_firstTokenTimer->start();
    m_overallTimer->start();
}

void OllamaBackend::preload(const QString &model)
{
    m_auxQueue.enqueue(AuxJob{AuxJob::Kind::Preload, model, {}});
    pumpAuxQueue();
}

void OllamaBackend::showModel(const QString &model)
{
    m_auxQueue.enqueue(AuxJob{AuxJob::Kind::Show, model, {}});
    pumpAuxQueue();
}

void OllamaBackend::probe()
{
    const QUrl url(baseUrlTrimmed(m_config) + QStringLiteral("/api/tags"));
    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, &OllamaBackend::onProbeFinished);
}

void OllamaBackend::parseStreamLine(const QByteArray &line)
{
    if (line.isEmpty() || !m_streamReply) {
        return;
    }
    logStreamLine(line, m_streamPostTimer.elapsed());

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("error"))) {
        finishStreamTurn(false, obj.value(QStringLiteral("error")).toString());
        return;
    }

    const QJsonObject message = obj.value(QStringLiteral("message")).toObject();
    const QString thinking = message.value(QStringLiteral("thinking")).toString();
    const QString content = message.value(QStringLiteral("content")).toString();
    if (!thinking.isEmpty() && content.isEmpty() && m_streamText.isEmpty() && m_streamReply) {
        if (!m_currentRequestSentThinkFalse && !m_turnThinkingRetryUsed) {
            m_modelsRequireThinkFalse.insert(m_activeRequest.model);
            m_turnThinkingRetryUsed = true;
            m_pendingThinkFalseRetry = true;
            qCInfo(lcOllama) << m_streamPurpose
                             << "thinking-only chunk with no content; retrying once with think:false";
            abortStreamForThinkingRetry();
            return;
        }
        if (m_currentRequestSentThinkFalse) {
            qCWarning(lcOllama) << m_streamPurpose
                                << "thinking field non-empty despite think:false; ignored";
        }
    }
    if (!content.isEmpty()) {
        if (!m_gotFirstToken) {
            m_gotFirstToken = true;
            m_firstTokenMs = m_requestTimer.elapsed();
            m_firstTokenTimer->stop();
            qCInfo(lcOllama) << m_streamPurpose << "first content token at" << m_firstTokenMs
                             << "ms";
        }
        m_streamText.append(content);
        emit token(static_cast<int>(m_turnId), content);
    }

    if (obj.value(QStringLiteral("done")).toBool()) {
        LlmMetrics metrics;
        metrics.promptEvalCount = obj.value(QStringLiteral("prompt_eval_count")).toInteger();
        if (obj.contains(QStringLiteral("prompt_eval_cached_count"))) {
            metrics.promptEvalCachedCount =
                obj.value(QStringLiteral("prompt_eval_cached_count")).toInteger();
        }
        metrics.promptEvalNs = obj.value(QStringLiteral("prompt_eval_duration")).toInteger();
        metrics.evalCount = obj.value(QStringLiteral("eval_count")).toInteger();
        metrics.evalNs = obj.value(QStringLiteral("eval_duration")).toInteger();
        metrics.loadNs = obj.value(QStringLiteral("load_duration")).toInteger();
        metrics.totalNs = obj.value(QStringLiteral("total_duration")).toInteger();
        metrics.msToFirstToken = m_firstTokenMs;
        metrics.doneReason = obj.value(QStringLiteral("done_reason")).toString();
        m_overallTimer->stop();
        m_streamCompleted = true;

        if (m_streamText.isEmpty()) {
            finishStreamTurn(
                false,
                QStringLiteral(
                    "Ollama finished with no speakable content. If the model is Qwen 3.5 / "
                    "eagle-clm, ensure think:false is set (see eagle.ollama logs)."));
            return;
        }
        emit finished(static_cast<int>(m_turnId), m_streamText, metrics);
    }
}

void OllamaBackend::finishStreamTurn(bool success, const QString &error)
{
    m_overallTimer->stop();
    m_firstTokenTimer->stop();
    if (!success) {
        emit failed(static_cast<int>(m_turnId), error, true);
    }
}

void OllamaBackend::onStreamReadyRead()
{
    if (!m_streamReply) {
        return;
    }
    if (!m_loggedFirstStreamByte) {
        m_loggedFirstStreamByte = true;
        qCInfo(lcOllama) << m_streamPurpose << "first NDJSON bytes at"
                         << m_streamPostTimer.elapsed() << "ms HTTP"
                         << m_streamReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    m_lineBuffer.append(m_streamReply->readAll());
    int newlineIndex = -1;
    while ((newlineIndex = m_lineBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_lineBuffer.left(newlineIndex).trimmed();
        m_lineBuffer.remove(0, newlineIndex + 1);
        parseStreamLine(line);
    }
}

void OllamaBackend::cleanupStream()
{
    if (m_streamReply) {
        m_streamReply->deleteLater();
        m_streamReply = nullptr;
    }
    m_firstTokenTimer->stop();
    m_overallTimer->stop();
    m_lineBuffer.clear();
    m_streamPurpose.clear();
}

void OllamaBackend::onStreamFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    const bool isCurrent = reply == m_streamReply;
    if (!isCurrent && !m_pendingThinkFalseRetry) {
        reply->deleteLater();
        return;
    }
    if (isCurrent) {
        m_streamReply = nullptr;
    }
    reply->disconnect(this);

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qCInfo(lcOllama) << m_streamPurpose << "HTTP finished status=" << httpStatus
                     << "error=" << reply->error() << reply->errorString();

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        const bool retryThink = m_pendingThinkFalseRetry;
        m_pendingThinkFalseRetry = false;
        reply->deleteLater();
        m_lineBuffer.clear();
        m_streamPurpose.clear();
        m_firstTokenTimer->stop();
        m_overallTimer->stop();
        if (retryThink) {
            beginTurnStream();
        }
        return;
    }

    if (!m_lineBuffer.trimmed().isEmpty()) {
        onStreamReadyRead();
    }

    if (reply->error() != QNetworkReply::NoError && !m_streamCompleted) {
        handleModelFallback(reply->errorString());
        reply->deleteLater();
        m_lineBuffer.clear();
        m_streamPurpose.clear();
        m_firstTokenTimer->stop();
        m_overallTimer->stop();
        return;
    }

    if (!m_streamCompleted) {
        finishStreamTurn(false,
                         QStringLiteral("Ollama stream closed without done:true (possible proxy "
                                        "or truncated response)."));
    }

    reply->deleteLater();
    m_lineBuffer.clear();
    m_streamPurpose.clear();
    m_firstTokenTimer->stop();
    m_overallTimer->stop();
}

void OllamaBackend::handleModelFallback(const QString &errorText)
{
    const QString fallback = m_config.llmFallbackModel();
    if (m_activeRequest.model != fallback && m_installedModels.contains(fallback)
        && errorText.contains(QStringLiteral("not found"), Qt::CaseInsensitive)) {
        m_activeRequest.model = fallback;
        start(m_activeRequest);
        return;
    }
    emit failed(static_cast<int>(m_turnId), errorText, true);
}

void OllamaBackend::onFirstTokenTimeout()
{
    if (!m_gotFirstToken && m_streamReply) {
        finishStreamTurn(false,
                        QStringLiteral("Timed out waiting for the first speakable token from "
                                       "Ollama (content was empty; check think:false)."));
        safeAbortStream();
    }
}

void OllamaBackend::onOverallTimeout()
{
    if (m_streamReply) {
        finishStreamTurn(false, QStringLiteral("Ollama request timed out."));
        safeAbortStream();
    }
}

void OllamaBackend::onProbeFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit probed(false, {}, {});
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QStringList models;
    const QJsonArray arr = doc.object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue &value : arr) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            models.append(name);
        }
    }
    m_installedModels = models;
    emit probed(true, QString(), models);
}
