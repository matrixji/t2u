CC=gcc
CXX=g++

CFLAGS=-Wall -O2 -pipe -g

RM=/bin/rm -fr


all: libt2u.a t2u_test	

libt2u.a: t2u.o 

t2u_test: t2u_test.o t2u.o
	$(CXX) $^ -o $@ -lpthread -levent

%.o: %.cpp 
	$(CXX) -c $^ -o $@ $(CFLAGS) 


clean:
	$(RM) *.o libt2u.a t2u_test
