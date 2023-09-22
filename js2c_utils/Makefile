all: test

test: test.c
	clang -o $@ $^

test.c: test.js
	./v8_js2c $^

clean:
	rm -f test test.c
