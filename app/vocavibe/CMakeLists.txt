# Contest 2026 team 243 VocaVibe app
if(CONFIG_LVX_USE_DEMO_CONTEST2026_243_VOCAVIBE)
  nuttx_add_application(
    NAME vocavibe
    SRCS src/vocavibe_main.c
    INCLUDE_DIRECTORIES include
    STACKSIZE 2048)
endif()
