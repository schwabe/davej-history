CROSS_COMPILE = s390-

hwc_measure: hwc_measure.c
	$(CROSS_COMPILE)gcc -o $@ $^

hwc_cntl_key: hwc_cntl_key.c
	$(CROSS_COMPILE)gcc -o $@ $^

clean:
	rm -f hwc_measure
	rm -f hwc_cntl_key

