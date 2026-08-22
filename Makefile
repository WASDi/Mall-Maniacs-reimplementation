# Incremental build for maniac_rebuild.exe (Mall Maniacs replacement).
# 32-bit Windows, original system libraries only: KERNEL32, USER32, GDI32, WINMM, DSOUND.
# Compile each source file to an object, track header deps, link once.

CC      = i686-w64-mingw32-gcc
CFLAGS  = -m32 -O2 -g -Wall -Wextra -mwindows
CFLAGS += -MMD -MP
LIBS    = -lkernel32 -luser32 -lgdi32 -lwinmm -ldsound

OUT     = /home/wasd/MallManiacsUnmodified/maniac_rebuild.exe
SRCDIR  = src
OBJDIR  = obj

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

all: $(OUT)

$(OUT): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o "$@"
	@ls -la "$@"

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

$(OBJDIR):
	@mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(OUT)

-include $(DEPS)

.PHONY: all clean
