all: dasdfmt

dasdfmt: dasdfmt.c
	$(CROSS_COMPILE)gcc -I$(HPATH) -o $@ $^
	$(CROSS_COMPILE)strip $@

clean:
	rm -f dasdfmt

