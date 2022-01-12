PORT=2731
CFLAGS= -DPORT=\$(PORT) -g -Wall
OBJ = xmodemserver.o crc16.o helper.o
DEPENDENCIES = crc16.h xmodemserver.h helper.h

xmodemserver: ${OBJ}
	gcc ${CFLAGS} -o $@ $^ -lm

%.o : %.c ${DEPENDENCIES}
	gcc ${CFLAGS} -c $<

clean :
	rm -f *.o xmodemserver crc16 helper