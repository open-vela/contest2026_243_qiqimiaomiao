# Contest 2026 team 243 VocaVibe app
if(CONFIG_LVX_USE_DEMO_CONTEST2026_243_VOCAVIBE)
  nuttx_add_application(
    NAME vocavibe
    SRCS src/vocavibe_main.c src/vocavibe_boot.c src/vocavibe_core.c src/vocavibe_ui.c src/vocavibe_font.c src/vocavibe_touch.c src/vocavibe_html.c
    INCLUDE_DIRECTORIES include ${NUTTX_APPS_DIR}/netutils/cjson/cJSON
    DEPENDS lvgl
    STACKSIZE 32768)
endif()
