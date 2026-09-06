# dmenu-wl version
VERSION = 5.0.0

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

# Wayland protocol tools
WAYLAND_SCANNER = wayland-scanner
WAYLAND_PROTOCOLS = ${shell pkg-config --variable=pkgdatadir wayland-protocols}

# dependencies
PKG_CONFIG = pkg-config
PKGS = cairo glib-2.0 gobject-2.0 pango pangocairo wayland-client xkbcommon

# includes and libs
INCS = ${shell ${PKG_CONFIG} --cflags ${PKGS}}
LIBS = ${shell ${PKG_CONFIG} --libs ${PKGS}} -lrt

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -DVERSION=\"${VERSION}\"
CFLAGS = -std=c99 -pedantic -Wall -Os ${INCS} ${CPPFLAGS}
LDFLAGS = ${LIBS}

# compiler and linker
CC = cc
