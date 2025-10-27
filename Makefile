CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0`
SRCS = src/main.c src/ui_init.c src/sim_manager.c src/sim_engine.c src/algorithms.c \
	src/instr_parser.c src/ui_view.c src/visualization_draw.c src/util.c src/config.c
OBJS = $(SRCS:.c=.o)
TARGET = pager_sim

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS) -lm

clean:
	rm -f $(OBJS) $(TARGET)
