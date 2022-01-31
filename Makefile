CC=egcc
CFLAGS=-g

HEADERS = nyaxy.h

OFILES = network.o main.o

OUTPUT = nyaxy

nyaxy: $(OFILES)
	$(CC) $(CFLAGS) -o $@ $(OFILES)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o $(OUTPUT)
