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
}

OllamaBackend::~OllamaBackend()
{
    abort();
}

bool OllamaBackend::isBusy() const
{
    return m_streamReply != nullptr || m_auxReply != nullptr;
}

void OllamaBackend::abort()
{
    if (m_streamReply) {
        m_streamReply->abort();
        cleanupStream();
    }
    if (m_auxReply) {
        m_auxReply->abort();
        m_auxReply->deleteLater();
        m_auxReply = nullptr;
    }
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

QNetworkReply *OllamaBackend::postChat(const QJsonObject &body, const QString &purpose,
                                       bool streamResponse)
{
    if (m_auxReply) {
        qCWarning(lcOllama) << "Aux request" << purpose << "blocked: another aux request is active:"
                            << m_auxPurpose;
        return nullptr;
    }

    logChatRequest(purpose, body);
    const QUrl url(baseUrlTrimmed(m_config) + QStringLiteral("/api/chat"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_auxPurpose = purpose;
    m_auxReply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (!streamResponse) {
        connect(m_auxReply, &QNetworkReply::finished, this, &OllamaBackend::onSimpleChatFinished);
    } else {
        connect(m_auxReply, &QNetworkReply::readyRead, this, [this]() {
            if (m_auxReply) {
                m_auxReply->readAll();
            }
        });
        connect(m_auxReply, &QNetworkReply::finished, this, &OllamaBackend::onSimpleChatFinished);
    }
    return m_auxReply;
}

void OllamaBackend::runStartupSequence(const QString &model, const QString &fallbackModel,
                                       const QString &systemPrompt)
{
    m_startupPrimaryModel = model;
    m_startupFallbackModel = fallbackModel;
    m_startupSystemPrompt = systemPrompt;
    m_startupStep = StartupStep::ShowPrimary;
    showModel(model);
}

void OllamaBackend::advanceStartupSequence()
{
    switch (m_startupStep) {
    case StartupStep::ShowPrimary:
        if (!m_startupFallbackModel.isEmpty()
            && m_startupFallbackModel != m_startupPrimaryModel) {
            m_startupStep = StartupStep::ShowFallback;
            showModel(m_startupFallbackModel);
        } else {
            m_startupStep = StartupStep::Preload;
            preload(m_startupPrimaryModel);
        }
        break;
    case StartupStep::ShowFallback:
        m_startupStep = StartupStep::Preload;
        preload(m_startupPrimaryModel);
        break;
    case StartupStep::Preload:
        m_startupStep = StartupStep::WarmUp;
        warmUp(m_startupPrimaryModel, m_startupSystemPrompt);
        break;
    case StartupStep::WarmUp:
        m_startupStep = StartupStep::Idle;
        emit startupSequenceFinished(true);
        break;
    case StartupStep::Idle:
        break;
    }
}

void OllamaBackend::start(const LlmRequest &req)
{
    if (m_auxReply) {
        qCWarning(lcOllama) << "Turn start while aux busy (" << m_auxPurpose
                            << "); aborting aux to avoid Ollama slot deadlock";
        m_auxReply->abort();
        m_auxReply->deleteLater();
        m_auxReply = nullptr;
        m_auxPurpose.clear();
    }

    abort();
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
    m_pendingPreloadModel = model;
    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), QJsonArray());
    body.insert(QStringLiteral("stream"), false);
    body.insert(QStringLiteral("keep_alive"), m_config.llmKeepAlive());
    if (!postChat(body, QStringLiteral("preload"), false)) {
        m_pendingPreloadModel.clear();
        if (m_startupStep == StartupStep::Preload) {
            advanceStartupSequence();
        }
    }
}

void OllamaBackend::warmUp(const QString &model, const QString &systemPrompt)
{
    QVector<LlmMessage> messages;
    LlmMessage system;
    system.role = QStringLiteral("system");
    system.content = systemPrompt;
    messages.append(system);
    LlmMessage user;
    user.role = QStringLiteral("user");
    user.content = QStringLiteral("Reply with one word: Ready.");
    messages.append(user);

    LlmRequest req;
    req.model = model;
    req.messages = messages;
    req.purpose = QStringLiteral("warmup");
    req.numPredict = 2;
    req.temperature = 0.1;

    m_pendingWarmModel = model;
    m_pendingWarmSystem = systemPrompt;

    const QJsonObject body = buildChatBody(model, messages, req, true, false);
    if (!postChat(body, QStringLiteral("warmup"), false)) {
        m_pendingWarmModel.clear();
        m_pendingWarmSystem.clear();
        emit warmUpFinished(false);
        if (m_startupStep == StartupStep::WarmUp) {
            m_startupStep = StartupStep::Idle;
            emit startupSequenceFinished(false);
        }
    }
}

void OllamaBackend::probe()
{
    const QUrl url(baseUrlTrimmed(m_config) + QStringLiteral("/api/tags"));
    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, &OllamaBackend::onProbeFinished);
}

void OllamaBackend::showModel(const QString &model)
{
    if (m_auxReply) {
        qCWarning(lcOllama) << "showModel(" << model << ") skipped: aux busy with" << m_auxPurpose;
        if (m_startupStep != StartupStep::Idle) {
            advanceStartupSequence();
        }
        return;
    }

    const QUrl url(baseUrlTrimmed(m_config) + QStringLiteral("/api/show"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    m_auxPurpose = QStringLiteral("show:%1").arg(model);
    m_auxReply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_auxReply, &QNetworkReply::finished, this, &OllamaBackend::onShowFinished);
}

void OllamaBackend::parseStreamLine(const QByteArray &line)
{
    if (line.isEmpty()) {
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
                             << "thinking-only chunk with no content; retrying once with think:false for model"
                             << m_activeRequest.model;
            m_streamReply->abort();
            return;
        }
        if (m_currentRequestSentThinkFalse) {
            qCWarning(lcOllama) << m_streamPurpose
                                << "thinking field non-empty despite think:false; ignored for speech";
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
    if (!m_streamReply) {
        return;
    }

    const int httpStatus =
        m_streamReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qCInfo(lcOllama) << m_streamPurpose << "HTTP finished status=" << httpStatus
                     << "error=" << m_streamReply->error()
                     << m_streamReply->errorString();

    if (m_streamReply->error() == QNetworkReply::OperationCanceledError) {
        const bool retryThink = m_pendingThinkFalseRetry;
        m_pendingThinkFalseRetry = false;
        cleanupStream();
        if (retryThink) {
            beginTurnStream();
        }
        return;
    }

    if (!m_lineBuffer.trimmed().isEmpty()) {
        onStreamReadyRead();
    }

    if (m_streamReply->error() != QNetworkReply::NoError && !m_streamCompleted) {
        handleModelFallback(m_streamReply->errorString());
        cleanupStream();
        return;
    }

    if (!m_streamCompleted) {
        finishStreamTurn(false,
                         QStringLiteral("Ollama stream closed without done:true (possible proxy "
                                        "or truncated response)."));
    }

    cleanupStream();
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
        abort();
    }
}

void OllamaBackend::onOverallTimeout()
{
    if (m_streamReply) {
        finishStreamTurn(false, QStringLiteral("Ollama request timed out."));
        abort();
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

void OllamaBackend::onShowFinished()
{
    QNetworkReply *reply = m_auxReply;
    m_auxReply = nullptr;
    const QString purpose = m_auxPurpose;
    m_auxPurpose.clear();
    if (!reply) {
        return;
    }

    const QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    qCInfo(lcOllama) << purpose << "HTTP status=" << httpStatus << "ok=" << ok;
    if (!ok) {
        if (m_startupStep != StartupStep::Idle) {
            advanceStartupSequence();
        }
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(data);
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

    if (m_startupStep == StartupStep::ShowPrimary || m_startupStep == StartupStep::ShowFallback) {
        advanceStartupSequence();
    }
}

void OllamaBackend::onSimpleChatFinished()
{
    QNetworkReply *reply = m_auxReply;
    m_auxReply = nullptr;
    const QString purpose = m_auxPurpose;
    m_auxPurpose.clear();
    if (!reply) {
        return;
    }
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    qCInfo(lcOllama) << purpose << "HTTP status=" << httpStatus << "ok=" << ok;

    const bool warm = purpose == QStringLiteral("warmup");
    const bool preload = purpose == QStringLiteral("preload");

    if (warm) {
        emit warmUpFinished(ok);
        m_pendingWarmModel.clear();
        m_pendingWarmSystem.clear();
        if (m_startupStep == StartupStep::WarmUp) {
            advanceStartupSequence();
        }
    } else if (preload) {
        emit preloaded(m_pendingPreloadModel, ok);
        m_pendingPreloadModel.clear();
        if (m_startupStep == StartupStep::Preload) {
            advanceStartupSequence();
        }
    }
}
