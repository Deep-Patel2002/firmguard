CC ?= cc
CLANG ?= clang
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=2
CPPFLAGS += -Iinclude
BUILD := build

.PHONY: all test sanitize differential fuzz bench clean format-check rust
all: $(BUILD)/fg-inspect

$(BUILD):
	mkdir -p $@

$(BUILD)/parse.o: src/parse.c include/firmguard.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(BUILD)/libfirmguard.a: $(BUILD)/parse.o
	ar rcs $@ $^

$(BUILD)/fg-inspect: cli/main.c cli/io.h include/firmguard.h $(BUILD)/libfirmguard.a
	$(CC) $(CPPFLAGS) $(CFLAGS) cli/main.c $(BUILD)/libfirmguard.a -o $@

$(BUILD)/test-parser: tests/test_parser.c $(BUILD)/libfirmguard.a
	$(CC) $(CPPFLAGS) -DFG_ALLOCATION_WRAP $(CFLAGS) -pthread tests/test_parser.c $(BUILD)/libfirmguard.a -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -o $@

rust:
	cargo build --manifest-path rust/Cargo.toml --release --locked

test: $(BUILD)/test-parser
	$(BUILD)/test-parser
	cargo test --manifest-path rust/Cargo.toml --locked

sanitize: | $(BUILD)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer src/parse.c cli/main.c -o $(BUILD)/fg-inspect-sanitize
	$(CC) $(CPPFLAGS) -DFG_ALLOCATION_WRAP -DFG_SKIP_ALLOC_WRAP -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_parser.c src/parse.c -pthread -o $(BUILD)/test-parser-sanitize
	ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-parser-sanitize

differential: $(BUILD)/fg-inspect rust
	python3 tools/differential.py --c $(BUILD)/fg-inspect --rust rust/target/release/fg-rust --output docs/verification/latest-differential.json

fuzz: | $(BUILD)
	$(CLANG) $(CPPFLAGS) -std=c11 -O1 -g -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer tests/fuzz_parse.c src/parse.c -o $(BUILD)/fuzz-parse
	python3 tools/run_fuzz.py --binary $(BUILD)/fuzz-parse --seconds 30 --output docs/verification/latest-fuzz.json

bench: $(BUILD)/fg-inspect rust
	python3 tools/benchmark.py --c $(BUILD)/fg-inspect --rust rust/target/release/fg-rust --output docs/verification/latest-benchmark.json

format-check:
	if command -v clang-format >/dev/null 2>&1; then clang-format --dry-run --Werror src/parse.c cli/main.c tests/test_parser.c tests/fuzz_parse.c include/firmguard.h; fi
	cargo fmt --manifest-path rust/Cargo.toml --all -- --check

clean:
	rm -rf $(BUILD) rust/target
