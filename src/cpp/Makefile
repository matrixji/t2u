CC=gcc
CXX=g++

CXXFLAGS=-Wall -O0 -pipe -ggdb3 -Wstrict-aliasing=0 -std=c++0x

RM=/bin/rm -fr


all: libt2u.a #t2u_test	

libt2u.a: t2u_message.o t2u.o

t2u_test: t2u_test.o 
	test -h libstdc++.a || ln -s `g++ -print-file-name=libstdc++.a` 
	$(CXX) $^ -o $@ -L. -lpthread -Wl,-Bstatic -lev -Wl,-Bdynamic 

%.o: %.cpp
	$(CXX) -c $^ -o $@ $(CXXFLAGS) 


clean:
	$(RM) *.o libt2u.a t2u_test
	unlink libstdc++.a 
