SRCS := md_frag.c
OUT := md_frag

build:
	gcc -std=c99 -Wall -Wextra -Wpedantic $(SRCS) -o $(OUT)

build-debug:
	gcc -std=c99 -Wall -Wextra -Wpedantic -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer $(SRCS) -o $(OUT)

clean:
	rm -f $(OUT)

test: build
	@for dir in tests/*/; do \
		name=$$(basename "$$dir"); \
		echo "=== Test: $$name ==="; \
		./$(OUT) -c "$$dir/chunks.in" -s "$$dir/sizes.in"; \
		echo; \
	done

test-debug: build-debug
	@for dir in tests/*/; do \
		name=$$(basename "$$dir"); \
		echo "=== Test: $$name ==="; \
		./$(OUT) -c "$$dir/chunks.in" -s "$$dir/sizes.in"; \
		echo; \
	done
