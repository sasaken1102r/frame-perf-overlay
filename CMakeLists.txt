cmake_minimum_required(VERSION 3.16)
project(frame-perf-overlay VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SteamVR に同梱の OpenVR ライブラリ（Frame の SteamOS 上のパス）
set(OPENVR_LIB_DIR "/opt/steamvr/bin/linuxarm64" CACHE PATH "libopenvr_api.so のあるディレクトリ")

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
pkg_check_modules(LIBNL REQUIRED IMPORTED_TARGET libnl-genl-3.0)

# core/: platform-free C++17 (metric registry, value store, demand-driven scheduler, the widget layout and its edits)
add_library(frame-perf-core STATIC
  core/reason.cpp
  core/metric_registry.cpp
  core/metric_store.cpp
  core/scheduler.cpp
  core/json.cpp
  core/layout.cpp
  core/layout_edit.cpp
  core/atomic_file.cpp
)
target_include_directories(frame-perf-core PUBLIC core)
target_compile_options(frame-perf-core PRIVATE -Wall -Wextra)

# core/render/: drawing helpers, the widgets and the grid renderer (cairo + freetype only)
add_library(frame-perf-render STATIC
  core/render/draw.cpp
  core/render/theme.cpp
  core/render/widget_text.cpp
  core/render/widgets.cpp
  core/render/grid_renderer.cpp
)
target_include_directories(frame-perf-render PUBLIC core/render)
target_link_libraries(frame-perf-render PUBLIC frame-perf-core PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_options(frame-perf-render PRIVATE -Wall -Wextra)

add_executable(frame-perf-overlay
  headset/main.cpp
  headset/placement.cpp
  headset/clock.cpp
  headset/config.cpp
  headset/sensors.cpp
  headset/sensor_math.cpp
  headset/system_metrics.cpp
  headset/sensor_bridge.cpp
  headset/process_scan.cpp
  headset/hide_rules.cpp
  headset/dump_metrics.cpp
  headset/vr_overlay.cpp
  headset/vk_texture.cpp
  headset/settings_panel.cpp
  headset/layout_editor.cpp
  headset/layout_authority.cpp
  headset/profile_store.cpp
  headset/wifi_link.cpp
  headset/i18n.cpp
  headset/autostart.cpp
  headset/paths.cpp
  vendor/frame-updater/cpp/update_check.cpp
)
target_include_directories(frame-perf-overlay PRIVATE headset third_party/openvr vendor/frame-updater/cpp)
target_compile_options(frame-perf-overlay PRIVATE -Wall -Wextra)
# Release の最適化。例外や浮動小数点の扱いは変えない（OFF にすると速さ優先の -O3 ＋ LTO）。
# 静的ライブラリ（core/・core/render/）にも同じものをかける（かけないと gc と LTO がその分を落とせない）
option(FRAME_PERF_OPTIMIZE_SIZE "Optimize the release overlay for installed size" ON)
include(CheckIPOSupported)
check_ipo_supported(RESULT PERF_IPO_SUPPORTED OUTPUT PERF_IPO_ERROR)
foreach(target frame-perf-overlay frame-perf-core frame-perf-render)
  if(FRAME_PERF_OPTIMIZE_SIZE)
    target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-Os>)
  endif()
  target_compile_options(${target} PRIVATE
    $<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>)
  if(PERF_IPO_SUPPORTED)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  endif()
endforeach()
target_link_options(frame-perf-overlay PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections>)
# --version で出すバージョン（上の project(VERSION) から）
target_compile_definitions(frame-perf-overlay PRIVATE FRAME_PERF_OVERLAY_VERSION="${PROJECT_VERSION}")
target_link_libraries(frame-perf-overlay PRIVATE
  frame-perf-render
  PkgConfig::CAIRO
  PkgConfig::FREETYPE
  PkgConfig::VULKAN
  PkgConfig::LIBNL
  "${OPENVR_LIB_DIR}/libopenvr_api.so"
)
# 実行時も SteamVR 同梱のライブラリを使う
# （ビルドした時点から INSTALL_RPATH だけにする。ビルド用の RPATH だと末尾に空の要素が付き、
#   空の要素はカレントディレクトリからもライブラリを探す意味になるため）
set_target_properties(frame-perf-overlay PROPERTIES
  BUILD_WITH_INSTALL_RPATH TRUE
  INSTALL_RPATH "${OPENVR_LIB_DIR}"
)

install(TARGETS frame-perf-overlay RUNTIME DESTINATION bin)

# テスト（手首への固定の計算・時計・設定ファイル・性能パネル・設定パネルの当たり判定・センサー・画素の変換、
# ウィジェットのレイアウトと編集・読み取りの予定）。ctest --test-dir build で動かす。
# リリースのビルド（scripts/package.sh）は -DBUILD_TESTING=OFF でテストを作らない
include(CTest)
if(BUILD_TESTING)
  # The dashboard tab and everything it pulls in (the settings panel, the layout editor, the config)
  set(PERF_DASHBOARD_SOURCES
    headset/placement.cpp
    headset/clock.cpp
    headset/config.cpp
    headset/settings_panel.cpp
    headset/layout_editor.cpp
    headset/i18n.cpp
    headset/wifi_link.cpp
  )

  add_executable(wrist-clock-test tests/wrist_clock_test.cpp ${PERF_DASHBOARD_SOURCES})
  target_include_directories(wrist-clock-test PRIVATE headset third_party/openvr vendor/frame-updater/cpp)
  target_compile_options(wrist-clock-test PRIVATE -Wall -Wextra)
  target_link_libraries(wrist-clock-test PRIVATE frame-perf-render PkgConfig::LIBNL)
  add_test(NAME wrist-clock COMMAND wrist-clock-test)

  add_executable(runtime-test tests/runtime_test.cpp headset/placement.cpp headset/config.cpp headset/i18n.cpp)
  target_include_directories(runtime-test PRIVATE headset core/render third_party/openvr)
  target_compile_options(runtime-test PRIVATE -Wall -Wextra)
  target_link_libraries(runtime-test PRIVATE frame-perf-core PkgConfig::VULKAN)
  add_test(NAME runtime COMMAND runtime-test)

  add_executable(sensors-test tests/sensors_test.cpp headset/sensors.cpp headset/sensor_math.cpp headset/wifi_link.cpp)
  target_include_directories(sensors-test PRIVATE headset)
  target_compile_options(sensors-test PRIVATE -Wall -Wextra)
  target_link_libraries(sensors-test PRIVATE PkgConfig::LIBNL)
  add_test(NAME sensors COMMAND sensors-test)

  add_executable(ui-helpers-test tests/ui_helpers_test.cpp headset/config.cpp headset/i18n.cpp)
  target_include_directories(ui-helpers-test PRIVATE headset)
  target_compile_options(ui-helpers-test PRIVATE -Wall -Wextra)
  target_link_libraries(ui-helpers-test PRIVATE frame-perf-render)
  add_test(NAME ui-helpers COMMAND ui-helpers-test)

  # core/: platform-free
  foreach(name core-registry core-scheduler core-json core-layout core-layout-edit)
    string(REPLACE "-" "_" file ${name})
    add_executable(${name}-test tests/${file}_test.cpp)
    target_compile_options(${name}-test PRIVATE -Wall -Wextra)
    target_link_libraries(${name}-test PRIVATE frame-perf-core)
    add_test(NAME ${name} COMMAND ${name}-test)
  endforeach()

  # core/render/: the widgets and the grid
  foreach(name render-grid render-rate-text)
    string(REPLACE "-" "_" file ${name})
    add_executable(${name}-test tests/${file}_test.cpp)
    target_compile_options(${name}-test PRIVATE -Wall -Wextra)
    target_link_libraries(${name}-test PRIVATE frame-perf-render)
    add_test(NAME ${name} COMMAND ${name}-test)
  endforeach()

  add_executable(layout-editor-test tests/layout_editor_test.cpp ${PERF_DASHBOARD_SOURCES})
  target_include_directories(layout-editor-test PRIVATE headset third_party/openvr vendor/frame-updater/cpp)
  target_compile_options(layout-editor-test PRIVATE -Wall -Wextra)
  target_link_libraries(layout-editor-test PRIVATE frame-perf-render PkgConfig::LIBNL)
  add_test(NAME layout-editor COMMAND layout-editor-test)

  add_executable(config-layout-test tests/config_layout_test.cpp headset/config.cpp headset/i18n.cpp)
  target_include_directories(config-layout-test PRIVATE headset core/render)
  target_compile_options(config-layout-test PRIVATE -Wall -Wextra)
  target_link_libraries(config-layout-test PRIVATE frame-perf-core)
  add_test(NAME config-layout COMMAND config-layout-test)

  add_executable(profile-store-test tests/profile_store_test.cpp headset/profile_store.cpp
                 headset/layout_authority.cpp headset/config.cpp headset/i18n.cpp)
  target_include_directories(profile-store-test PRIVATE headset core/render)
  target_compile_options(profile-store-test PRIVATE -Wall -Wextra)
  target_link_libraries(profile-store-test PRIVATE frame-perf-core)
  add_test(NAME profile-store COMMAND profile-store-test)

  # The system sources end to end: a fake /sys + /proc tree, the scheduler, the metric store
  add_executable(system-sources-test tests/system_sources_test.cpp headset/sensors.cpp headset/sensor_math.cpp
                 headset/system_metrics.cpp headset/wifi_link.cpp)
  target_include_directories(system-sources-test PRIVATE headset)
  target_compile_options(system-sources-test PRIVATE -Wall -Wextra)
  target_link_libraries(system-sources-test PRIVATE frame-perf-core PkgConfig::LIBNL)
  add_test(NAME system-sources COMMAND system-sources-test)

  # System sensor parsing and derivations: pure, fed from tests/fixtures/sensors
  add_executable(sensor-math-test tests/sensor_math_test.cpp headset/sensor_math.cpp)
  target_include_directories(sensor-math-test PRIVATE headset)
  target_compile_options(sensor-math-test PRIVATE -Wall -Wextra)
  add_test(NAME sensor-math COMMAND sensor-math-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sensors)

  # The /proc process scan (fixture trees) and the Wi-Fi channel / band / width / combine helpers
  add_executable(process-wifi-test tests/process_wifi_test.cpp headset/process_scan.cpp headset/hide_rules.cpp
                 headset/wifi_link.cpp)
  target_include_directories(process-wifi-test PRIVATE headset)
  target_compile_options(process-wifi-test PRIVATE -Wall -Wextra)
  target_link_libraries(process-wifi-test PRIVATE frame-perf-core PkgConfig::LIBNL)
  add_test(NAME process-wifi COMMAND process-wifi-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

  # Hide system: glob / rules file / defaults vs the captured Frame process list / scanner drops hidden pids
  add_executable(hide-rules-test tests/hide_rules_test.cpp headset/process_scan.cpp headset/hide_rules.cpp)
  target_include_directories(hide-rules-test PRIVATE headset)
  target_compile_options(hide-rules-test PRIVATE -Wall -Wextra)
  target_link_libraries(hide-rules-test PRIVATE frame-perf-core)
  add_test(NAME hide-rules COMMAND hide-rules-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
endif()
