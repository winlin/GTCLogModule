CC=gcc
#CC=arm-linux-androideabi-gcc

MitLogTest: test.c MITLogModule.c
	$(CC) -std=c99 -o MitLogTest test.c MITLogModule.c -lpthread
clean:
	-rm *.o MitLogTest *.gch
