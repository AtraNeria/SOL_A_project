CC = gcc
FLAGS = -Wall
OBJECTS = api.o client.o server.o main.o list.o
TARGETS = main.out client.out

.PHONY: all clean cleanall


list.o: list.c list.h
	$(CC) $(FLAGS) $< -c -o $@


api.o: api.c client_api.h commProtocol.h list.h
	$(CC) $(FLAGS) $< -c -o $@


client.o: client.c client_api.h
	$(CC) $(FLAGS) $< -c -o $@


server.o: server.c server.h commProtocol.h list.h
	$(CC) $(FLAGS) $< -c -o $@


main.o: main.c
	$(CC) $(FLAGS) $< -c -o $@




main.out: main.o server.o list.o
	$(CC) $(FLAGS) -pthread $^ -o $@


client.out: api.o client.o list.o
	$(CC) $(FLAGS) $^ -o $@


all: $(TARGETS)


clean:
	-rm -f $(OBJECTS)

cleanall:
	-rm -f $(OBJECTS) $(TARGETS)
