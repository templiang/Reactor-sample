CC=g++
SERVER=epoll_server

all:$(SERVER)
$(SERVER):$(SERVER).cc
	$(CC) -o $@ $^ -std=c++11 -ljsoncpp

.PHONY:clean
clean:
	rm -f $(SERVER)