CFLAGS=`pkg-config --cflags gtk+-3.0`
LIBS=`pkg-config --libs gtk+-3.0` -rdynamic -lm

.phony: all clean

all: prog.out
	./prog.out

prog.out: main.o
	cc -O3 main.o -o prog.out $(LIBS)

main.o: main.c
	cc -c -O3 main.c -o main.o $(CFLAGS)

clean:
	-@rm *.o