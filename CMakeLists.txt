cmake_minimum_required(VERSION 3.21)

project(EagleGD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia)

set(EAGLEGD_SOURCES
    src/app/main.cpp
    src/app/Application.cpp
    src/app/AppConfig.cpp
    src/app/Paths.cpp
    src/app/Logging.cpp
    src/app/ServiceSupervisor.cpp
    src/ui/MainWindow.cpp
    src/ui/ChatPanel.cpp
    src/ui/ReadinessPanel.cpp
    src/chatbubble.cpp
    src/ollamaclient.cpp
    src/speechinput.cpp
    src/piperspeaker.cpp
    resources/resources.qrc
)

qt_add_executable(EagleGD ${EAGLEGD_SOURCES})

target_include_directories(EagleGD PRIVATE src src/app src/ui)

target_link_libraries(EagleGD PRIVATE Qt6::Widgets Qt6::Network Qt6::Multimedia)

if(WIN32)
    set_target_properties(EagleGD PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS EagleGD RUNTIME DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/EagleGD/resources)
