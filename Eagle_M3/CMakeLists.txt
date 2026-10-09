cmake_minimum_required(VERSION 3.21)

project(EagleGD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia Test)

set(EAGLEGD_LLM_SOURCES
    src/llm/ILlmBackend.cpp
    src/llm/TemplateEngine.cpp
    src/llm/TokenEstimator.cpp
    src/llm/HistoryManager.cpp
    src/llm/PromptBuilder.cpp
    src/llm/OllamaBackend.cpp
)

set(EAGLEGD_APP_SOURCES
    src/app/main.cpp
    src/app/Application.cpp
    src/app/AppConfig.cpp
    src/app/Paths.cpp
    src/app/Logging.cpp
    src/app/ServiceSupervisor.cpp
    src/app/LlmServices.cpp
    src/app/ChildProcessGuard.cpp
    src/app/WhisperServerProcess.cpp
    src/app/SttConfig.cpp
)

set(EAGLEGD_UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/ChatPanel.cpp
    src/ui/ReadinessPanel.cpp
    src/ui/MetricsOverlay.cpp
    src/ui/ListenController.cpp
)

set(EAGLEGD_CORE_SOURCES
    src/core/Metrics.cpp
    src/core/HindiFeminineFixer.cpp
    src/core/TextPostProcessor.cpp
    src/core/SentenceStreamer.cpp
    src/core/TurnSpeechController.cpp
    src/core/LanguageService.cpp
)

set(EAGLEGD_STT_SOURCES
    src/stt/Vad.cpp
    src/stt/PcmTo16kMono.cpp
    src/stt/WavUtil.cpp
    src/stt/TranscriptAssembler.cpp
    src/stt/WhisperServerClient.cpp
    src/stt/SttEngine.cpp
)

set(EAGLEGD_TTS_SOURCES
    src/tts/SpeechQueuePlayer.cpp
    src/tts/PiperCliBackend.cpp
    src/tts/TtsEngine.cpp
    src/tts/FillerCache.cpp
)

set(EAGLEGD_PROTOTYPE_SOURCES
    src/chatbubble.cpp
    src/piperspeaker.cpp
)

qt_add_executable(EagleGD
    ${EAGLEGD_APP_SOURCES}
    ${EAGLEGD_UI_SOURCES}
    ${EAGLEGD_CORE_SOURCES}
    ${EAGLEGD_TTS_SOURCES}
    ${EAGLEGD_STT_SOURCES}
    ${EAGLEGD_LLM_SOURCES}
    ${EAGLEGD_PROTOTYPE_SOURCES}
    resources/resources.qrc
)

target_include_directories(EagleGD PRIVATE src src/app src/ui src/llm src/core src/tts src/stt)

target_link_libraries(EagleGD PRIVATE Qt6::Widgets Qt6::Network Qt6::Multimedia)

if(WIN32)
    set_target_properties(EagleGD PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS EagleGD RUNTIME DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/EagleGD/resources)

enable_testing()

set(EAGLE_TEST_LIBS
    Qt6::Test
    Qt6::Core
    Qt6::Network
)

set(EAGLE_TEST_PROMPT_SOURCES
    src/app/AppConfig.cpp
    src/app/Paths.cpp
    src/llm/TemplateEngine.cpp
    src/llm/TokenEstimator.cpp
    src/llm/PromptBuilder.cpp
    resources/resources.qrc
)

set(EAGLE_TEST_HISTORY_SOURCES
    src/app/AppConfig.cpp
    src/llm/TokenEstimator.cpp
    src/llm/HistoryManager.cpp
    resources/resources.qrc
)

function(eagle_add_unit_test name sources)
    qt_add_executable(${name} tests/unit/${name}.cpp ${sources})
    target_include_directories(${name} PRIVATE src src/app src/llm src/core src/tts src/stt)
    target_link_libraries(${name} PRIVATE ${EAGLE_TEST_LIBS})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

set(EAGLE_TEST_OLLAMA_SOURCES
    src/app/AppConfig.cpp
    src/llm/ILlmBackend.cpp
    src/llm/OllamaBackend.cpp
)

eagle_add_unit_test(tst_promptbuilder "${EAGLE_TEST_PROMPT_SOURCES}")
eagle_add_unit_test(tst_historymanager "${EAGLE_TEST_HISTORY_SOURCES}")
eagle_add_unit_test(tst_ollamabackend "${EAGLE_TEST_OLLAMA_SOURCES}")

set(EAGLE_TEST_TEXTPOST_SOURCES
    src/core/HindiFeminineFixer.cpp
    src/core/TextPostProcessor.cpp
)
set(EAGLE_TEST_STREAMER_SOURCES
    src/core/HindiFeminineFixer.cpp
    src/core/TextPostProcessor.cpp
    src/core/SentenceStreamer.cpp
)

eagle_add_unit_test(tst_hindifixer src/core/HindiFeminineFixer.cpp)
eagle_add_unit_test(tst_textpostprocessor "${EAGLE_TEST_TEXTPOST_SOURCES}")
eagle_add_unit_test(tst_sentencestreamer "${EAGLE_TEST_STREAMER_SOURCES}")
eagle_add_unit_test(tst_speechqueueplayer src/tts/SpeechQueuePlayer.cpp)
target_link_libraries(tst_speechqueueplayer PRIVATE Qt6::Multimedia)

# M3: speech input
eagle_add_unit_test(tst_vad src/stt/Vad.cpp)
eagle_add_unit_test(tst_languageservice "src/core/LanguageService.cpp;resources/resources.qrc")
eagle_add_unit_test(tst_pcmto16kmono src/stt/PcmTo16kMono.cpp)
eagle_add_unit_test(tst_transcriptassembler src/stt/TranscriptAssembler.cpp)
eagle_add_unit_test(tst_whisperclient "src/stt/WhisperServerClient.cpp;src/stt/WavUtil.cpp")
eagle_add_unit_test(tst_whisperserverprocess
    "src/app/WhisperServerProcess.cpp;src/app/ChildProcessGuard.cpp;src/stt/WhisperServerClient.cpp;src/stt/WavUtil.cpp")
target_compile_definitions(tst_whisperserverprocess PRIVATE
    EAGLE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
eagle_add_unit_test(tst_sttengine
    "src/stt/SttEngine.cpp;src/stt/Vad.cpp;src/stt/PcmTo16kMono.cpp;src/stt/WavUtil.cpp;src/stt/TranscriptAssembler.cpp;src/stt/WhisperServerClient.cpp;src/core/LanguageService.cpp")
target_link_libraries(tst_sttengine PRIVATE Qt6::Multimedia)
