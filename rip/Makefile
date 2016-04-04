CC=g++
FLAG=-Wall
THREAD=-lpthread
TIMER=-lrt

all: app

app: node.o router.o app.o 
	$(CC) -g $(FLAG) $^ -o $@ $(THREAD) $(TIMER)

node.o: node.cpp node.h router.h protocal.h
	$(CC) -c $< $(THREAD)

router.o: router.cpp router.h node.h protocal.h
	$(CC) -c $< $(THREAD) $(TIMER)

app.o: app.cpp
	$(CC) -c $<

clean:
	rm -f *~ *.o app
