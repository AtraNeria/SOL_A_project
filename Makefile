CC = gcc
FLAGS = -Wall
OBJECTS = api.o client.o server.o main.o
TARGETS = main.out client.out

.PHONY: all clean cleanall


api.o: api.c client_api.h
	$(CC) $(FLAGS) $< -c -o $@


client.o: client.c
	$(CC) $(FLAGS) $< -c -o $@


server.o: server.c server.h
	$(CC) $(FLAGS) $< -c -o $@


main.o: main.c
	$(CC) $(FLAGS) $< -c -o $@




main.out: main.o server.o
	$(CC) $(FLAGS) $^ -o $@


client.out: client.o api.o
	$(CC) $(FLAGS) $^ -o $@


all: $(TARGETS)


clean:
	-rm -f $(OBJECTS)

cleanall:
	-rm -f $(OBJECTS) $(TARGETS)
