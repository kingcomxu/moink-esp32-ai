cmake_minimum_required(VERSION 3.16)

set(MOINK_SRCS
    "main.c" "epd_drv.c" "settings.c" "netif_ap.c" "captive.c"
    "ota_web.c" "frame.c" "power.c" "store.c" "led.c")

idf_component_register(
    SRCS ${MOINK_SRCS}
    INCLUDE_DIRS "."
    # ★ ESP32 + IDF 6.x 注意：老式的 "driver" 大组件在 6.x 已按外设拆分，
    #   driver/gpio.h 归 esp_driver_gpio、driver/spi_master.h 归 esp_driver_spi，
    #   只写 driver 会直接报「header provided by xxx component」并编译失败。
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi
                  nvs_flash esp_wifi esp_netif esp_event
                  esp_http_server esp_adc esp_hw_support
                  app_update spi_flash esp_partition esp_timer lwip
)

# 改页 / 改版本号必须触发重编：显式声明隐式依赖（头文件扫描器不会把
# 「页面 -> 生成的 C 数组头」这条链算进来，漏了就会静默打进旧页面）。
set_property(SOURCE main.c APPEND PROPERTY OBJECT_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/index_html.h")
set_property(SOURCE ${MOINK_SRCS} APPEND PROPERTY OBJECT_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/version.h")

# ★ 纯 ESP-IDF 构建下的「改页自动固化」：page/index.html 或 version.h 比
#   index_html.h 新时，构建前自动跑一次 tools/make_index_header.py。
#   （原项目是 PlatformIO，靠人记得手动跑；这里挂进 CMake，漏跑不会发生。）
#   脚本内含「页面 meta 版本 == version.h 的 MOINK_VERSION」断言，
#   两边漂移会直接让构建失败 —— 这正是我们要的闸门。
idf_build_get_property(python PYTHON)
add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/index_html.h
    COMMAND  ${python} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/make_index_header.py
    DEPENDS  ${CMAKE_CURRENT_SOURCE_DIR}/../page/index.html
             ${CMAKE_CURRENT_SOURCE_DIR}/version.h
    COMMENT  "Embedding page/index.html -> main/index_html.h"
    VERBATIM
)
add_custom_target(moink_page DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/index_html.h)
add_dependencies(${COMPONENT_LIB} moink_page)

# R1.2.0（FB-015）：控制页一律内嵌（唯一真源 = page/index.html），
# 原有的 MOINK_EMBED_PAGE 逃生开关与占位页已删除 —— 设备必须自带控制页，
# 否则首次连接无页面可用。
