CC ?= clang
CFLAGS = -std=c11 -Wall -Wextra -pedantic -Iinclude -I/opt/homebrew/include -I/opt/homebrew/include/SDL2
LDFLAGS = -L/opt/homebrew/lib -lSDL2 -lSDL2_ttf

CORE_SRCS = src/dom.c \
            src/style.c \
            src/html_parser.c \
            src/css_parser.c \
            src/layout.c \
            src/cookie.c \
            src/net.c \
            src/window.c \
            src/renderer.c

CORE_OBJS = $(CORE_SRCS:.c=.o)

TEST_BINS = test_dom test_html test_css test_layout test_cookie test_integration test_script_trigger

.PHONY: all clean test run asan

all: minibrowser $(TEST_BINS)

minibrowser: $(CORE_OBJS) src/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test_dom: tests/test_dom.c src/dom.c src/style.c
	$(CC) $(CFLAGS) -o $@ $^

test_html: tests/test_html.c src/dom.c src/style.c src/html_parser.c
	$(CC) $(CFLAGS) -o $@ $^

test_css: tests/test_css.c src/dom.c src/style.c src/html_parser.c src/css_parser.c
	$(CC) $(CFLAGS) -o $@ $^

test_layout: tests/test_layout.c src/dom.c src/style.c src/html_parser.c src/css_parser.c src/layout.c
	$(CC) $(CFLAGS) -o $@ $^

test_cookie: tests/test_cookie.c src/cookie.c
	$(CC) $(CFLAGS) -o $@ $^

test_integration: tests/test_integration.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_script_trigger: tests/test_script_trigger.c $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TEST_BINS)
	@echo "========================================="
	@echo "        RUNNING COMPLETE TEST SUITE      "
	@echo "========================================="
	./test_dom
	./test_html
	./test_css
	./test_layout
	./test_cookie
	./test_integration
	./test_script_trigger
	@echo "========================================="
	@echo "     ALL TESTS PASSED (7/7 SUITES)       "
	@echo "========================================="

asan: CFLAGS += -fsanitize=address,undefined -g
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean test_dom test_html test_css test_layout test_cookie
	@echo "========================================="
	@echo "   RUNNING CORE ENGINE UNDER ASAN        "
	@echo "========================================="
	./test_dom
	./test_html
	./test_css
	./test_layout
	./test_cookie
	@echo "========================================="
	@echo "   ALL CORE MODULES PASSED UNDER ASAN    "
	@echo "========================================="

run: minibrowser
	./minibrowser examples/demo.html

clean:
	rm -f src/*.o minibrowser $(TEST_BINS)
