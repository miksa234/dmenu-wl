# dmenu-wl - dynamic menu for Wayland
# See LICENSE file for copyright and license details.

include config.mk

BUILDDIR = build
PROTO_C = ${BUILDDIR}/fractional-scale-v1-protocol.c \
	${BUILDDIR}/viewporter-protocol.c \
	${BUILDDIR}/xdg-shell-protocol.c \
	${BUILDDIR}/wlr-layer-shell-unstable-v1-protocol.c
PROTO_H = ${BUILDDIR}/fractional-scale-v1-client-protocol.h \
	${BUILDDIR}/viewporter-client-protocol.h \
	${BUILDDIR}/wlr-layer-shell-unstable-v1-client-protocol.h
DMENUOBJ = ${BUILDDIR}/dmenu.o ${BUILDDIR}/draw.o \
	${BUILDDIR}/fractional-scale-v1-protocol.o \
	${BUILDDIR}/viewporter-protocol.o \
	${BUILDDIR}/xdg-shell-protocol.o \
	${BUILDDIR}/wlr-layer-shell-unstable-v1-protocol.o

FRACTIONAL_SCALE_XML = ${WAYLAND_PROTOCOLS}/staging/fractional-scale/fractional-scale-v1.xml
VIEWPORTER_XML = ${WAYLAND_PROTOCOLS}/stable/viewporter/viewporter.xml
XDG_SHELL_XML = ${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml

all: options ${BUILDDIR}/dmenu-wl ${BUILDDIR}/dmenu-wl_path

options:
	@echo dmenu-wl build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

${BUILDDIR}/config.h:
	mkdir -p ${BUILDDIR}
	cp config.def.h $@

${BUILDDIR}:
	mkdir -p ${BUILDDIR}

${BUILDDIR}/fractional-scale-v1-protocol.c: ${FRACTIONAL_SCALE_XML} | ${BUILDDIR}
	${WAYLAND_SCANNER} private-code $< $@
${BUILDDIR}/fractional-scale-v1-client-protocol.h: ${FRACTIONAL_SCALE_XML} | ${BUILDDIR}
	${WAYLAND_SCANNER} client-header $< $@
${BUILDDIR}/viewporter-protocol.c: ${VIEWPORTER_XML} | ${BUILDDIR}
	${WAYLAND_SCANNER} private-code $< $@
${BUILDDIR}/viewporter-client-protocol.h: ${VIEWPORTER_XML} | ${BUILDDIR}
	${WAYLAND_SCANNER} client-header $< $@
${BUILDDIR}/xdg-shell-protocol.c: ${XDG_SHELL_XML} | ${BUILDDIR}
	${WAYLAND_SCANNER} private-code $< $@
${BUILDDIR}/wlr-layer-shell-unstable-v1-protocol.c: wlr-layer-shell-unstable-v1.xml | ${BUILDDIR}
	${WAYLAND_SCANNER} private-code $< $@
${BUILDDIR}/wlr-layer-shell-unstable-v1-client-protocol.h: wlr-layer-shell-unstable-v1.xml | ${BUILDDIR}
	${WAYLAND_SCANNER} client-header $< $@

${BUILDDIR}/dmenu.o: ${BUILDDIR}/config.h draw.h ${PROTO_H}
${BUILDDIR}/draw.o: draw.h ${PROTO_H}

${BUILDDIR}/%.o: %.c config.mk | ${BUILDDIR}
	mkdir -p ${BUILDDIR}
	${CC} -I${BUILDDIR} ${CPPFLAGS} ${CFLAGS} -c -o $@ $<

${BUILDDIR}/dmenu-wl: ${DMENUOBJ}
	${CC} ${LDFLAGS} -o $@ ${DMENUOBJ} ${LDLIBS}

${BUILDDIR}/dmenu-wl_path: ${BUILDDIR}/dmenu_path.o
	${CC} -o $@ ${BUILDDIR}/dmenu_path.o


clean:
	rm -rf ${BUILDDIR} dmenu-wl-${VERSION}.tar.gz

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

.PHONY: all options clean dist install uninstall
