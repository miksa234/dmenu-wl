# dmenu-wl - dynamic menu for Wayland
# See LICENSE file for copyright and license details.

include config.mk

BUILDDIR = build
PROTO_C = ${BUILDDIR}/xdg-shell-protocol.c \
	${BUILDDIR}/xdg-output-unstable-v1-protocol.c \
	${BUILDDIR}/wlr-layer-shell-unstable-v1-protocol.c
PROTO_H = ${BUILDDIR}/xdg-shell-client-protocol.h \
	${BUILDDIR}/xdg-output-unstable-v1-client-protocol.h \
	${BUILDDIR}/wlr-layer-shell-unstable-v1-client-protocol.h
DMENUOBJ = ${BUILDDIR}/dmenu.o ${BUILDDIR}/draw.o \
	${BUILDDIR}/xdg-shell-protocol.o \
	${BUILDDIR}/xdg-output-unstable-v1-protocol.o \
	${BUILDDIR}/wlr-layer-shell-unstable-v1-protocol.o

all: options ${BUILDDIR}/dmenu-wl ${BUILDDIR}/dmenu-wl_path

options:
	@echo dmenu-wl build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

${BUILDDIR}/config.h:
	mkdir -p ${BUILDDIR}
	cp config.def.h $@

${PROTO_C} ${PROTO_H}: protocols

protocols:
	mkdir -p ${BUILDDIR}
	${WAYLAND_SCANNER} public-code \
		${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml \
		${BUILDDIR}/xdg-shell-protocol.c
	${WAYLAND_SCANNER} client-header \
		${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml \
		${BUILDDIR}/xdg-shell-client-protocol.h
	${WAYLAND_SCANNER} public-code \
		${WAYLAND_PROTOCOLS}/unstable/xdg-output/xdg-output-unstable-v1.xml \
		${BUILDDIR}/xdg-output-unstable-v1-protocol.c
	${WAYLAND_SCANNER} client-header \
		${WAYLAND_PROTOCOLS}/unstable/xdg-output/xdg-output-unstable-v1.xml \
		${BUILDDIR}/xdg-output-unstable-v1-client-protocol.h
	${WAYLAND_SCANNER} public-code wlr-layer-shell-unstable-v1.xml \
		${BUILDDIR}/wlr-layer-shell-unstable-v1-protocol.c
	${WAYLAND_SCANNER} client-header wlr-layer-shell-unstable-v1.xml \
		${BUILDDIR}/wlr-layer-shell-unstable-v1-client-protocol.h

${BUILDDIR}/%.o: %.c ${BUILDDIR}/config.h ${PROTO_H} config.mk draw.h
	mkdir -p ${BUILDDIR}
	${CC} -I${BUILDDIR} -c ${CFLAGS} -o $@ $<

${BUILDDIR}/dmenu-wl: ${DMENUOBJ}
	${CC} -o $@ ${DMENUOBJ} ${LDFLAGS}

${BUILDDIR}/dmenu-wl_path: ${BUILDDIR}/dmenu_path.o
	${CC} -o $@ ${BUILDDIR}/dmenu_path.o


clean:
	rm -rf ${BUILDDIR} dmenu-wl dmenu-wl_path dmenu-wl-${VERSION}.tar.gz \
		config.h *.o \
		xdg-shell-protocol.c xdg-shell-client-protocol.h \
		xdg-output-unstable-v1-protocol.c xdg-output-unstable-v1-client-protocol.h \
		wlr-layer-shell-unstable-v1-protocol.c \
		wlr-layer-shell-unstable-v1-client-protocol.h

dist: clean
	mkdir -p dmenu-wl-${VERSION}
	cp LICENSE Makefile README.md config.def.h config.mk dmenu-wl.1 \
		dmenu-wl_run dmenu_path.c draw.h wlr-layer-shell-unstable-v1.xml \
		dmenu.c draw.c dmenu-wl-${VERSION}
	tar -cf dmenu-wl-${VERSION}.tar dmenu-wl-${VERSION}
	gzip dmenu-wl-${VERSION}.tar
	rm -rf dmenu-wl-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${BUILDDIR}/dmenu-wl ${BUILDDIR}/dmenu-wl_path dmenu-wl_run ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/dmenu-wl \
		${DESTDIR}${PREFIX}/bin/dmenu-wl_path \
		${DESTDIR}${PREFIX}/bin/dmenu-wl_run
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < dmenu-wl.1 > ${DESTDIR}${MANPREFIX}/man1/dmenu-wl.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/dmenu-wl.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dmenu-wl \
		${DESTDIR}${PREFIX}/bin/dmenu-wl_path \
		${DESTDIR}${PREFIX}/bin/dmenu-wl_run \
		${DESTDIR}${MANPREFIX}/man1/dmenu-wl.1

.PHONY: all options clean dist install uninstall protocols
