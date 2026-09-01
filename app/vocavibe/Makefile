############################################################################
# Contest 2026 team 243 VocaVibe app - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = vocavibe
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 8192
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_243_VOCAVIBE)

CSRCS   = src/vocavibe_bt.c
MAINSRC = src/vocavibe_main.c
CFLAGS  += -I$(APPDIR)/packages/demos/contest2026_243_vocavibe/include

include $(APPDIR)/Application.mk
