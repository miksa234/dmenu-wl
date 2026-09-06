# dmenu-wl - dynamic menu

dmenu-wl is a small dynamic menu for wlroots-based Wayland compositors.

## Requirements

The compositor must implement wlr-layer-shell version 4, `wl_output` version
4, fractional-scale-v1, and viewporter.

Building requires GNU Make, pkg-config, wayland-scanner, wayland-protocols,
wayland-client, Cairo, Pango, and xkbcommon. The layer-shell protocol XML is
bundled because it is distributed separately by wlr-protocols.

## Installation

```sh
make
sudo make install
```

## Usage

`dmenu-wl` reads newline-separated items from standard input and prints the
selected item. See `dmenu-wl(1)` for the complete interface.

To launch programs from `PATH`, bind `dmenu-wl_run` in the compositor. For
example, in Sway:

```text
bindsym $mod+d exec dmenu-wl_run -i
```

## Nix

```sh
nix shell github:miksa234/dmenu-wl
```
