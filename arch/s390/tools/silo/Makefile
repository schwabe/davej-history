all: silo

silo: silo.o cfg.o
	$(CROSS_COMPILE)gcc -o $@ $^

clean:
	rm -f *.o silo

