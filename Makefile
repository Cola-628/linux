CC = gcc
CFLAGS = -Wall -g
OBJ = main.o tap.o arp.o ip.o tcp.o socket.o icmp.o
TARGET = mini-tcpipnew

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o $(TARGET)