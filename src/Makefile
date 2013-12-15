CC=gcc
CXX=g++

CXXFLAGS=-Wall -O2 -pipe -g -Wstrict-aliasing=0

RM=/bin/rm -fr


all: libt2u.a t2u_test	

libt2u.a: t2u.o 

t2u_test: t2u_test.o t2u.o
	test -h libstdc++.a || ln -s `g++ -print-file-name=libstdc++.a` 
	$(CXX) $^ -o $@ -L. -lpthread -Wl,-Bstatic -lev -Wl,-Bdynamic 

%.o: %.cpp 
	$(CXX) -c $^ -o $@ $(CXXFLAGS) 


clean:
	$(RM) *.o libt2u.a t2u_test
	unlink libstdc++.a 
