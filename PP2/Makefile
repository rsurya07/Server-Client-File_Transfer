all: server client

server: server.o
	gcc server.o -g -o server -pthread

server.o: server.c
	gcc -g -c server.c 

client: client.o
	gcc client.o -g -o client -pthread

client.o: client.c
	gcc -g -c client.c

clean:
	rm *.o server client


