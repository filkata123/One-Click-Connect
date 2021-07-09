PROG = PiReceive
CC = g++
CPPFLAGS = -Wall
LDFLAGS = -lwiringPi
SRC = PiReceive.cpp 

all:
	$(CC) -o $(PROG) $(SRC) $(LDFLAGS) $(CPPFLAGS)
clean:
	rm -f $(PROG) $(OBJS)