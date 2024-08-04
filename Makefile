ALL = lpr-modoki

all:	$(ALL)

lpr-modoki: lpr-modoki.c
	$(CC) -O2 -Wall -Wno-pointer-sign $< -o $@

clean:
	rm -f $(ALL)
