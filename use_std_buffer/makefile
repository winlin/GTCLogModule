CC=gcc
#CC=arm-linux-androideabi-gcc
LIBS= -lpthread
DEFS= -D_GNU_SOURCE
DEFINES= $(INCLUDES) $(defs) -DSYS_UNIX=1
CFLAGS= $(DEFINES) -Wall -O2 -std=gnu99 

MitLogTest: test.c mit_log_module.c
	$(CC) $(CFLAGS) -o MitLogTest test.c mit_log_module.c mit_log_module.h $(LIBS)
clean:
	@rm *.o MitLogTest *.gch
