CROSS_COMPILE = s390-

all: hwc_cntl_key

hwc_cntl_key: hwc_cntl_key.c
	$(CROSS_COMPILE)gcc -o $@ $^
	$(STRIP) $@

clean:
	rm -f hwc_cntl_key.c

