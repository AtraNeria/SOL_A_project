CC = gcc
FLAGS = -Wall -g
OBJECTS = api.o client.o server.o main.o list.o protocol.o
TARGETS = main.out client.out

.PHONY: all clean cleanall test1 cleantest


protocol.o: protocol.c commProtocol.h
	$(CC) $(FLAGS) $< -c -o $@

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




main.out: main.o server.o list.o protocol.o
	$(CC) $(FLAGS) -pthread $^ -o $@


client.out: api.o client.o list.o protocol.o
	$(CC) $(FLAGS) $^ -o $@


all: $(TARGETS)


clean:
	-rm -f $(OBJECTS)

cleanall:
	-rm -f $(OBJECTS) $(TARGETS)




test1:
	make all
	mv ./main.out ./client.out ./Test1

test2:
	make all
	mv ./main.out ./client.out ./Test2

cleantest1:
	rm ./Test1/log.txt ./Test1/server ./Test1/client.out ./Test1/main.out
	rm ./Test1/ReadFile/*
	make cleanall

cleantest2:
	rm ./Test2/log.txt ./Test2/server ./Test2/client.out ./Test2/main.out
	rm ./Test2/Read/*
	rm ./Test2/Expelled/*
	make cleanall

cleantestAll:
	rm ./Test1/log.txt ./Test1/server ./Test1/client.out ./Test1/main.out
	rm ./Test1/ReadFile/*
	rm ./Test2/log.txt ./Test2/server ./Test2/client.out ./Test2/main.out
	rm ./Test2/Read/*
	make cleanall

