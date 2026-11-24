CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
dnw_probe: dnw_probe.c
	$(CC) $(CFLAGS) $< -o $@ -lusb-1.0
.PHONY: clean
clean:
	rm -f dnw_probe
