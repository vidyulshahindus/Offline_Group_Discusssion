#pragma once

#include <memory>

#include <QStringList>

class AppConfig;
class HistoryManager;
class Metrics;
class OllamaBackend;
class Paths;
class PromptBuilder;
class TokenEstimator;

struct LlmServices {
    std::unique_ptr<TokenEstimator> estimator;
    std::unique_ptr<PromptBuilder> prompts;
    std::unique_ptr<HistoryManager> history;
    std::unique_ptr<OllamaBackend> backend;
    std::unique_ptr<Metrics> metrics;

    bool initialize(const AppConfig &config, const Paths &paths, QStringList *errorsOut = nullptr);
    ~LlmServices();
};
