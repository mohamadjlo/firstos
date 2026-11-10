# Bare-Metal Browser Dual-Target Makefile
# Target 1: FirstOS 32-bit x86 ELF (Freestanding, -nostdlib)
# Target 2: Host Simulation Runner (Off-screen virtual framebuffer & verification)

CROSS_CC = /opt/homebrew/bin/x86_64-elf-gcc
CROSS_LD = /opt/homebrew/bin/x86_64-elf-ld
HOST_CC  = cc

HTML_SRCS  = html_entities.c html_tokenizer.c html_parser.c bb_html.c
CSS_SRCS   = css_tokenizer.c css_selectors.c css_parser.c css_cascade.c style_resolver.c bb_css.c
PAINT_SRCS = painter.c
HIT_TEST_SRCS = hit_test.c
IMAGE_SRCS = bb_image.c

COMMON_SRCS = bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c bb_http.c bb_browser.c

# FirstOS Baremetal Flags
FIRSTOS_SRCS = $(COMMON_SRCS) bb_osal_firstos.c main_firstos.c
FIRSTOS_OBJS = $(FIRSTOS_SRCS:.c=.firstos.o)
FIRSTOS_CFLAGS = -m64 -mcmodel=small -mno-red-zone -ffreestanding -fno-pic -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -O2
FIRSTOS_LDFLAGS = -m elf_x86_64 -T linker.ld

# Host Simulation Flags
HOST_SRCS = $(COMMON_SRCS) bb_osal_host.c main_host.c
HOST_OBJS = $(HOST_SRCS:.c=.host.o)
HOST_CFLAGS = -DBB_HOST_BUILD -Wall -Wextra -O2 -g

TEST_SRCS = tests/test_libc.c tests/test_rasterizer.c tests/test_html_css.c tests/test_layout.c tests/test_universal_engine.c tests/test_html_frontend.c tests/test_css_frontend.c tests/test_painter.c tests/test_hit_test.c tests/test_image.c
TEST_BINS = test_libc_bin test_rasterizer_bin test_html_css_bin test_layout_bin test_universal_engine_bin test_html_frontend_bin test_css_frontend_bin test_painter_bin test_hit_test_bin test_image_bin

all: host firstos tests

host: baremetal_browser_host

baremetal_browser_host: $(HOST_OBJS)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_OBJS) -o $@

%.host.o: %.c
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

firstos: baremetal_browser.elf

baremetal_browser.elf: $(FIRSTOS_OBJS) linker.ld
	$(CROSS_LD) $(FIRSTOS_LDFLAGS) $(FIRSTOS_OBJS) -o $@

%.firstos.o: %.c
	$(CROSS_CC) $(FIRSTOS_CFLAGS) -c $< -o $@

tests: $(TEST_BINS)
	./test_libc_bin
	./test_rasterizer_bin
	./test_html_css_bin
	./test_layout_bin
	./test_universal_engine_bin
	./test_html_frontend_bin
	./test_css_frontend_bin
	./test_painter_bin
	./test_hit_test_bin
	./test_image_bin

test_libc_bin: tests/test_libc.c bb_libc.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 tests/test_libc.c bb_libc.c -o $@

test_rasterizer_bin: tests/test_rasterizer.c bb_libc.c bb_rasterizer.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 tests/test_rasterizer.c bb_libc.c bb_rasterizer.c -o $@

test_html_css_bin: tests/test_html_css.c bb_libc.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS)
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 tests/test_html_css.c bb_libc.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) -o $@

test_layout_bin: tests/test_layout.c bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 tests/test_layout.c bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c -o $@

test_universal_engine_bin: tests/test_universal_engine.c bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 -g \
		tests/test_universal_engine.c \
		bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c \
		-o $@

test_html_frontend_bin: tests/test_html_frontend.c bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 -g \
		tests/test_html_frontend.c \
		bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c \
		-o $@

test_css_frontend_bin: tests/test_css_frontend.c bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 -g \
		tests/test_css_frontend.c \
		bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c \
		-o $@

test_painter_bin: tests/test_painter.c bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 -g \
		tests/test_painter.c \
		bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c \
		-o $@

test_hit_test_bin: tests/test_hit_test.c bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 -g \
		tests/test_hit_test.c \
		bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c \
		-o $@

test_image_bin: tests/test_image.c bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c
	$(HOST_CC) -DBB_HOST_BUILD -Wall -Wextra -O2 -g \
		tests/test_image.c \
		bb_libc.c bb_rasterizer.c bb_font.c $(IMAGE_SRCS) bb_dom.c $(HTML_SRCS) $(CSS_SRCS) $(PAINT_SRCS) $(HIT_TEST_SRCS) bb_layout.c \
		-o $@

run_host: host
	./baremetal_browser_host

clean:
	rm -f *.o *.host.o *.firstos.o baremetal_browser_host baremetal_browser.elf $(TEST_BINS) *.bmp

.PHONY: all host firstos tests run_host clean
