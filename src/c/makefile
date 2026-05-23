CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g -I.
LDFLAGS = -lz -lssl -lcrypto 

SRCS = main.c commands.c config.c repository.c ref.c objects.c index.c ignore.c remote.c
OBJS = $(SRCS:.c=.o)
TARGET = fit

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
