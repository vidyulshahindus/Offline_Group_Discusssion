#include "LlmServices.h"

#include <QStringList>

#include "AppConfig.h"
#include "HistoryManager.h"
#include "Metrics.h"
#include "OllamaBackend.h"
#include "Paths.h"
#include "PromptBuilder.h"
#include "TokenEstimator.h"

LlmServices::~LlmServices() = default;

bool LlmServices::initialize(const AppConfig &config, const Paths &paths, QStringList *errorsOut)
{
    estimator = std::make_unique<TokenEstimator>(config);
    prompts = std::make_unique<PromptBuilder>(config, paths, *estimator);
    if (!prompts->loadTemplates()) {
        if (errorsOut) {
            errorsOut->append(QStringLiteral("Failed to load prompt templates."));
        }
        return false;
    }
    const QStringList validation = prompts->validationErrors();
    if (!validation.isEmpty() && errorsOut) {
        errorsOut->append(validation);
    }
    history = std::make_unique<HistoryManager>(config, *estimator);
    backend = std::make_unique<OllamaBackend>(config);
    metrics = std::make_unique<Metrics>();
    return true;
}
