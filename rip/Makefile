CC=g++
FLAG=-Wall
THREAD=-lpthread
TIMER=-lrt

all: app

app: node.o router.o app.o 
	$(CC) -g -std=c++0x $(FLAG) $^ -o $@ $(THREAD) $(TIMER)

node.o: node.cpp node.h router.h protocal.h
	$(CC) -c -std=c++0x $< $(THREAD)

router.o: router.cpp router.h node.h protocal.h
	$(CC) -c -std=c++0x $< $(THREAD) $(TIMER)

app.o: app.cpp
	$(CC) -c -std=c++0x $<

clean:
	rm -f *~ *.o app
