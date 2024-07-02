CC = gcc
CFLAGS = -Wall -pthread -lncurses
TARGET = hovercooked

all: $(TARGET)

$(TARGET): hovercooked.o
	$(CC) -o $(TARGET) hovercooked.o $(CFLAGS)

hovercooked.o: hovercooked.c
	$(CC) -c hovercooked.c $(CFLAGS)

clean:
	rm -f *.o $(TARGET)
