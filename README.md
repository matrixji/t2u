t2u
===

t2u is a udp tunnel for tcp based protocol.   
It's useful for using tcp for nat traversal while STUN with udp is available.   
  
  
install on windows
------------------
download libevent2 from http://libevent.org/ , then extract in dir c.  
using nmake to build libt2u.lib and test_t2u.exe  
  
cd t2u\c  
nmake -f Makefile.nmake  
  
install on linux
----------------
download libevent2 from http://libevent.org/ , make and make install it.  
using make to build libt2u.a and test_t2u  
  
cd t2u/c  
make -f Makefile.linux  
