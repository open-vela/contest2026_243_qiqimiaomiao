# Contest 2026 team 243 VocaVibe app
if(CONFIG_LVX_USE_DEMO_CONTEST2026_243_VOCAVIBE)
  nuttx_add_application(
    NAME vocavibe
    SRCS src/vocavibe_main.c src/vocavibe_bt.c src/vocavibe_core.c
    INCLUDE_DIRECTORIES include ${NUTTX_APPS_DIR}/netutils/cjson/cJSON
    STACKSIZE 8192)
endif()
