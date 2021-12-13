CC = gcc
FLAGS = -Wall -g
OBJECTS = api.o client.o server.o main.o list.o protocol.o commProtocol.o
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
	mv main.out client.out Test1
	cd ./Test1; valgrind --leak-check=full ./main.out & ./test1.sh

test2:
	make all
	mv ./main.out ./client.out Test2
	cd ./Test2; ./main.out & ./test2.sh

test3:
	make all
	mv ./main.out ./client.out Test3
	cd ./Test3; ./main.out & ./test3.sh

test4:
	make all
	mv ./main.out ./client.out Test4
	cd ./Test4; ./main.out & ./test4.sh


cleantest1:
	make cleanall
	rm ./Test1/log.txt ./Test1/server ./Test1/client.out ./Test1/main.out
	rm ./Test1/ReadFile/*

cleantest2:
	make cleanall
	rm ./Test2/log.txt ./Test2/server ./Test2/client.out ./Test2/main.out
	rm ./Test2/Expelled_2nd/*
	rm ./Test2/Expelled/*

cleantest3:
	make cleanall
	rm ./Test3/log.txt ./Test3/server ./Test3/client.out ./Test3/main.out
	rm ./Read/*

cleantest4:
	make cleanall
	rm ./Test4/log.txt ./Test4/server ./Test4/client.out ./Test4/main.out
	rm ./Read/*

cleantestAll:
	make cleanall
	rm ./Test1/log.txt ./Test1/server ./Test1/client.out ./Test1/main.out
	rm ./Test1/ReadFile/*
	rm ./Test2/log.txt ./Test2/server ./Test2/client.out ./Test2/main.out
	rm ./Test2/Read/*
