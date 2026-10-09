#pragma once

#include "LanguageService.h"
#include "WhisperServerProcess.h"
#include "stt/SttEngine.h"

class AppConfig;
class Paths;

// Reads the "stt" / "audio" config sections (spec §7) into the STT classes.
namespace SttConfig {

SttSettings engineSettings(const AppConfig &config);
WhisperServerProcess::Settings serverSettings(const AppConfig &config, const Paths &paths,
                                              const QString &language);
LanguageOptions languageOptions(const AppConfig &config);
// stt.threads = 0: about half the logical cores (≈ physical cores), 2..8.
int autoThreads();
// Whisper's language for a UI language code: "en", "hi", ... or "auto".
QString whisperLanguageFor(const QString &uiLanguageCode);

} // namespace SttConfig
