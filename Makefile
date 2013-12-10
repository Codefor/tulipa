#
#depends:gcc4.2.1+;pthread;libevent;libevhtp;hiredis
#
#

CC = ${JUMBO_ROOT}/opt/gcc46/bin/cc
INCLUDE = -I/usr/local/include -I${JUMBO_ROOT}/include -I.
BIN = 
LIB = -L/usr/local/lib -L${JUMBO_ROOT}/lib -levent -levent_openssl -levent_pthreads -lpthread  -levhtp -lhiredis -lmysqlclient

CFLAGS = -D_GNU_SOURCE -Wall -g
#CFLAGS = -Wall -g -pg
#CFLAGS =  -D_GNU_SOURCE -Wall -O2 
LDFLAGS = 

TARGET = tulipa-trackd

all: $(TARGET)

tulipa-trackd: inifile.o pool.o util.o md5.o sha1.o log.o job.o thread.o trackd.o redisjob.o mysqljob.o
	$(CC) -o $@ $^ $(LIB) 

test:test.o md5.o sha1.o
	$(CC) -o $@ $^ $(LIB) 

mysqltest:mysqljob.o
	$(CC) -o $@ $^ $(LIB) 

%.o : %.c	
	$(CC) -c $(CFLAGS) $< $(INCLUDE)

clean :
	$(RM) $(TARGET) test *.o

   

