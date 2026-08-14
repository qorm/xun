.PHONY: test clean

CFLAGS = -std=c11 -Wall -Wextra -O0 -g

test: xun_test
	./xun_test ..

xun_test: xun.c xun.h xun_test.c
	$(CC) $(CFLAGS) -o xun_test xun.c xun_test.c

clean:
	rm -f xun_test *.o
