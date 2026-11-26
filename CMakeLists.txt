cmake_minimum_required(VERSION 3.21)

project(PoeTradeSearch VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(PTS_BUILD_TESTS "Build unit tests" ON)
option(PTS_BUILD_APP "Build the GUI application" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network)

# ---- core (UI/플랫폼 없는 로직: 파서, 쿼리, 네트워크)
add_library(pts_core STATIC
    src/core/Types.h
    src/core/TextUtil.h src/core/TextUtil.cpp
    src/core/JsonUtil.h src/core/JsonUtil.cpp
    src/core/Keywords.h src/core/Keywords.cpp
    src/core/TradeDb.h src/core/TradeDb.cpp
    src/core/CheckedStore.h src/core/CheckedStore.cpp
    src/core/ItemParser.h src/core/ItemParser.cpp
    src/core/QueryBuilder.h src/core/QueryBuilder.cpp
    src/core/Config.h src/core/Config.cpp
    src/core/PriceSummary.h src/core/PriceSummary.cpp
    src/core/SearchDefaults.h src/core/SearchDefaults.cpp
    src/core/MatcherDb.h src/core/MatcherDb.cpp
    src/core/PriceEstimate.h src/core/PriceEstimate.cpp
    src/net/Endpoints.h src/net/Endpoints.cpp
    src/net/RateLimiter.h src/net/RateLimiter.cpp
    src/net/TradeClient.h src/net/TradeClient.cpp
    src/net/DataManager.h src/net/DataManager.cpp
    src/net/IconCache.h src/net/IconCache.cpp
    resources/resources.qrc
)
target_include_directories(pts_core PUBLIC src)
target_link_libraries(pts_core PUBLIC Qt6::Core Qt6::Network)
target_compile_definitions(pts_core PUBLIC
    PTS_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
)
if(MSVC)
    target_compile_options(pts_core PUBLIC /W4 /utf-8 /permissive-)
else()
    target_compile_options(pts_core PUBLIC -Wall -Wextra)
endif()

# ---- 앱
if(PTS_BUILD_APP)
set(PTS_APP_SOURCES
    src/main.cpp
    src/app/Application.h src/app/Application.cpp
    src/platform/Platform.h
    src/platform/HotkeyManager.h src/platform/HotkeyManager.cpp
    src/ui/MainPopup.h src/ui/MainPopup.cpp
    src/ui/FilterRow.h src/ui/FilterRow.cpp
    src/ui/SettingsDialog.h src/ui/SettingsDialog.cpp
    src/ui/HotkeyEdit.h src/ui/HotkeyEdit.cpp
)
if(WIN32)
    list(APPEND PTS_APP_SOURCES src/platform/win/WinPlatform.cpp resources/app.rc)
else()
    list(APPEND PTS_APP_SOURCES src/platform/stub/StubPlatform.cpp)
endif()

qt_add_executable(PoeTradeSearch WIN32 ${PTS_APP_SOURCES})
target_link_libraries(PoeTradeSearch PRIVATE pts_core Qt6::Widgets Qt6::Gui)
if(WIN32)
    target_link_libraries(PoeTradeSearch PRIVATE user32 advapi32 shell32)
endif()
endif()

# ---- 디버그 CLI
add_executable(pts_cli tools/pts_cli.cpp)
target_link_libraries(pts_cli PRIVATE pts_core)

# ---- 테스트
if(PTS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE pts_core Qt6::Test)
    target_compile_definitions(test_core PRIVATE PTS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME test_core COMMAND test_core)
endif()
