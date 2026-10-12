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
)

set(EAGLEGD_UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/ChatPanel.cpp
    src/ui/ReadinessPanel.cpp
    src/ui/MetricsOverlay.cpp
)

set(EAGLEGD_CURRICULUM_SOURCES
    src/curriculum/JsonResource.cpp
    src/curriculum/UnitPack.cpp
    src/curriculum/UnitPackValidator.cpp
    src/curriculum/ClmReference.cpp
    src/curriculum/IbReference.cpp
    src/curriculum/UnitLibrary.cpp
    src/curriculum/UnitContextBuilder.cpp
    src/curriculum/CurriculumController.cpp
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
    src/stt/WavUtil.cpp
    src/stt/PcmTo16kMono.cpp
    src/stt/Vad.cpp
    src/stt/AudioCapture.cpp
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
    ${EAGLEGD_CURRICULUM_SOURCES}
    ${EAGLEGD_CORE_SOURCES}
    ${EAGLEGD_TTS_SOURCES}
    ${EAGLEGD_STT_SOURCES}
    ${EAGLEGD_LLM_SOURCES}
    ${EAGLEGD_PROTOTYPE_SOURCES}
    resources/resources.qrc
)

target_include_directories(EagleGD PRIVATE src src/app src/ui src/llm src/core src/curriculum src/tts src/stt)

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

set(EAGLE_TEST_PROMPT_CURRICULUM_SOURCES
    ${EAGLE_TEST_PROMPT_SOURCES}
    src/curriculum/JsonResource.cpp
    src/curriculum/IbReference.cpp
    src/curriculum/UnitContextBuilder.cpp
    src/curriculum/UnitPack.cpp
    src/curriculum/UnitPackValidator.cpp
)

eagle_add_unit_test(tst_promptbuilder "${EAGLE_TEST_PROMPT_CURRICULUM_SOURCES}")
target_include_directories(tst_promptbuilder PRIVATE src/curriculum)
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

eagle_add_unit_test(tst_vad src/stt/Vad.cpp)
set(EAGLE_TEST_LANG_SOURCES
    src/core/LanguageService.cpp
    src/app/AppConfig.cpp
    src/app/Paths.cpp
    resources/resources.qrc
)
eagle_add_unit_test(tst_languageservice "${EAGLE_TEST_LANG_SOURCES}")
target_include_directories(tst_languageservice PRIVATE src/app)

set(EAGLE_TEST_UNITPACK_SOURCES
    src/curriculum/JsonResource.cpp
    src/curriculum/UnitPack.cpp
    src/curriculum/UnitPackValidator.cpp
    resources/resources.qrc
)
eagle_add_unit_test(tst_unitpack "${EAGLE_TEST_UNITPACK_SOURCES}")
target_include_directories(tst_unitpack PRIVATE src/curriculum)
