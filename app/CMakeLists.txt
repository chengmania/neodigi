cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple)
pkg_check_modules(FFTW3 REQUIRED fftw3)

qt_add_executable(neodigi
    main.cpp
    MainWindow.cpp
    WaterfallWidget.cpp
    FLDigiClient.cpp
    FreqDisplay.cpp
    Sidebar.cpp
    MacroGrid.cpp
    StatusBar.cpp
    Logbook.cpp
    ModeSelector.cpp
    resources/neodigi.qrc
)

target_link_libraries(neodigi PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    ${PULSEAUDIO_LIBRARIES}
    ${FFTW3_LIBRARIES}
)

target_include_directories(neodigi PRIVATE
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${FFTW3_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

install(TARGETS neodigi DESTINATION bin)
install(FILES resources/style.qss DESTINATION share/neodigi)
