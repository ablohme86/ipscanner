cmake_minimum_required(VERSION 3.16)
project(ipscanner2 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Widgets Network Concurrent REQUIRED)

set(SOURCES
    src/main.cpp
    src/core/HostItem.h
    src/core/HostItem.cpp
    src/core/MacVendorLookup.h
    src/core/MacVendorLookup.cpp
    src/core/ArpReader.h
    src/core/ArpReader.cpp
    src/scanner/WakeOnLan.h
    src/scanner/WakeOnLan.cpp
    src/scanner/PortScanner.h
    src/scanner/PortScanner.cpp
    src/scanner/ScanWorker.h
    src/scanner/ScanWorker.cpp
    src/scanner/NetworkScanner.h
    src/scanner/NetworkScanner.cpp
    src/models/HostTableModel.h
    src/models/HostTableModel.cpp
    src/models/HostSortFilterProxyModel.h
    src/models/HostSortFilterProxyModel.cpp
    src/ui/PortScanDialog.h
    src/ui/PortScanDialog.cpp
    src/ui/WolDialog.h
    src/ui/WolDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/tui/TuiTheme.h
    src/tui/TerminalScreen.h
    src/tui/TerminalScreen.cpp
    src/tui/TuiApp.h
    src/tui/TuiApp.cpp
    resources/resources.qrc
)

add_executable(ipscanner ${SOURCES})

target_include_directories(ipscanner PRIVATE src)

target_link_libraries(ipscanner PRIVATE
    Qt5::Widgets
    Qt5::Network
    Qt5::Concurrent
)

# Test target
set(TEST_SOURCES
    tests/test_scanner.cpp
    src/core/HostItem.h
    src/core/HostItem.cpp
    src/core/MacVendorLookup.h
    src/core/MacVendorLookup.cpp
    src/core/ArpReader.h
    src/core/ArpReader.cpp
    src/scanner/WakeOnLan.h
    src/scanner/WakeOnLan.cpp
    src/scanner/PortScanner.h
    src/scanner/PortScanner.cpp
    src/scanner/ScanWorker.h
    src/scanner/ScanWorker.cpp
    src/scanner/NetworkScanner.h
    src/scanner/NetworkScanner.cpp
    resources/resources.qrc
)

add_executable(test_scanner ${TEST_SOURCES})
target_include_directories(test_scanner PRIVATE src)
target_link_libraries(test_scanner PRIVATE
    Qt5::Widgets
    Qt5::Network
    Qt5::Concurrent
)

install(TARGETS ipscanner DESTINATION bin)
