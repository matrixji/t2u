CC=gcc

CFLAGS=-Wall -O2 -pipe

RM=/bin/rm -fr


all: libt2u.a t2u_test	

libt2u.a: t2u.o rbtree.o

t2u_test: t2u_test.o

%.o: %.c 
	$(CC) -c $^ -o $@ $(CFLAGS)


clean:
	$(RM) *.o libt2u.a t2u_test
