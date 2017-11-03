CC        =  gcc 
CFLAGS1   =  -O3 -m32           # for production code
CFLAGS1   =  -g -m32            # for debugging
CFLAGS1   = -O3

# The flags above assume the target OS is Linux.  Another
# platform may require something different.

# The target name below should be changed for each version 
# of the gold standard or distribution code. 

TARGET =  	mydlx
OBJ = 		mem.o clib.o dlx.o stall.o

$(TARGET):	$(OBJ) 
		$(CC) $(CFLAGS1) $(OBJ) -o $(TARGET) -lm

mem.o:		mem.c mem.h
		$(CC) $(CFLAGS1) -c mem.c

dlx.o:		dlx.c dlx.h mem.h ops.h dlxdef.h stalldef.h
		$(CC) $(CFLAGS1) -c dlx.c

clib.o:		clib.c mem.h dlx.h
		$(CC) $(CFLAGS1) -c clib.c

stall.o:	stall.c stalldef.h
		$(CC) $(CFLAGS1) -c stall.c

clean:     
	   	rm *.o $(TARGET)
