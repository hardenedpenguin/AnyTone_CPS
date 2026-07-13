CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS += -Iinclude -Ithird_party
LDFLAGS ?=
PREFIX  ?= /usr/local
LIBS    ?= -lm -pthread

SRCS = src/main.c src/serial.c src/protocol.c src/image.c src/util.c \
       src/codeplug.c src/http.c src/serve.c src/schema.c src/schema_catalog.c \
       src/desktop.c third_party/cJSON.c
OBJS = $(SRCS:.c=.o)
BIN  = anytone

# Embed WebKitGTK desktop shell when available (Option B).
WEBKIT_CFLAGS := $(shell pkg-config --cflags webkit2gtk-4.1 2>/dev/null)
WEBKIT_LIBS   := $(shell pkg-config --libs webkit2gtk-4.1 2>/dev/null)
ifneq ($(WEBKIT_CFLAGS),)
  CPPFLAGS += -DANYTONE_HAS_DESKTOP $(WEBKIT_CFLAGS)
  LIBS     += $(WEBKIT_LIBS)
endif

.PHONY: all clean install uninstall schema deb lint

all: $(BIN)

schema:
	python3 scripts/compile_schema.py schema/d878uv2_v4.00.xml \
	  -o web/schema/d878uv2_v4.00.json \
	  --memmap-c src/memmap_d878uv2_v400.inc \
	  --memmap-symbol D878UV2_V400 \
	  --model D878UV2 --firmware 4.00
	python3 scripts/compile_schema.py schema/d878uv_v4.00.xml \
	  -o web/schema/d878uv_v4.00.json \
	  --memmap-c src/memmap_d878uv_v400.inc \
	  --memmap-symbol D878UV_V400 \
	  --model D878UV --firmware 4.00
	python3 scripts/compile_schema.py schema/d578uv_v1.21.xml \
	  -o web/schema/d578uv_v1.21.json \
	  --memmap-c src/memmap_d578uv_v121.inc \
	  --memmap-symbol D578UV_V121 \
	  --model D578UV --firmware 1.21

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

src/%.o: src/%.c include/*.h src/memmap_d878uv.inc src/memmap_d878uv2_v400.inc \
         src/memmap_d878uv_v400.inc src/memmap_d578uv_v121.inc
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

third_party/%.o: third_party/cJSON.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -d $(DESTDIR)$(PREFIX)/share/anytone/web
	cp -a web/. $(DESTDIR)$(PREFIX)/share/anytone/web/
	install -d $(DESTDIR)$(PREFIX)/share/applications
	install -m 644 packaging/anytone.desktop $(DESTDIR)$(PREFIX)/share/applications/anytone.desktop
	for size in 48 128 256 512; do \
		install -d $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${size}x$${size}/apps; \
		install -m 644 packaging/icons/hicolor/$${size}x$${size}/apps/anytone.png \
			$(DESTDIR)$(PREFIX)/share/icons/hicolor/$${size}x$${size}/apps/anytone.png; \
	done
	install -d $(DESTDIR)$(PREFIX)/lib/udev/rules.d
	install -m 644 udev/99-anytone.rules $(DESTDIR)$(PREFIX)/lib/udev/rules.d/99-anytone.rules

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -rf $(DESTDIR)$(PREFIX)/share/anytone
	rm -f $(DESTDIR)$(PREFIX)/share/applications/anytone.desktop
	for size in 48 128 256 512; do \
		rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${size}x$${size}/apps/anytone.png; \
	done
	rm -f $(DESTDIR)$(PREFIX)/lib/udev/rules.d/99-anytone.rules

deb:
	fakeroot dpkg-buildpackage -b -us -uc

lint: deb
	lintian --fail-on error \
		--suppress-tags bad-distribution-in-changes-file \
		../*.changes ../anytone_*.deb
