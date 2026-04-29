#   Copyright (C) 2026 Nicholas Ngai
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

TARGET = bddump
OBJS = bddump.o

CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c11 -pedantic -Wall -Wextra -Werror -O3
LDFLAGS =
LDLIBS = -lavcodec -lavformat -lavutil -lbluray

$(TARGET): $(OBJS)

clean: FORCE
	rm -rf $(TARGET) $(OBJS)

FORCE:
