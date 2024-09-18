TARGET = bddump
OBJS = bddump.o

CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c11 -pedantic -Wall -Wextra -Werror -O3
LDFLAGS =
LDLIBS = -lbluray

$(TARGET): $(OBJS)

clean: FORCE
	rm -rf $(TARGET) $(OBJS)

FORCE:
