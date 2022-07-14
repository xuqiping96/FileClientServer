OBJECTS = FileClient FileServer

all: $(OBJECTS)

.PHONY: all

FileClient: FileClient.c
	gcc -o $@ $<

FileServer: FileServer.c
	gcc -o $@ $<

clean:
	rm -f $(OBJECTS)