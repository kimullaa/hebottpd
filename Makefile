CC = gcc
CFLAGS = -Wall -O0 -g3

hebottpd: main.o service.o
	$(CC) $(CFLAGS) -o $@ $^

.c.o:
	$(CC) $(CFLAGS) -c $< 

clean: main.o service.o
	rm $^ hebottpd
