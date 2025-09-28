CC=gcc
CFLAGS=-Wall -Wextra -O2 -pthread

all: servidor cliente

servidor: servidor.c
	$(CC) $(CFLAGS) -o servidor servidor.c

cliente: cliente.c
	$(CC) $(CFLAGS) -o cliente cliente.c

clean:
	rm -f servidor cliente *.o *~
