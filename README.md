# dmenu-wl - dynamic menu

dmenu-wl is an efficient dynamic menu for wayland (wlroots).

## Requirements

Requires a compositor which implements version 4 of wlr-layer-shell, core
`wl_output` version 4, fractional-scale-v1, and viewporter. Compositors without
these modern protocols are intentionally unsupported.

Required libraries (and headers):

- wayland-client
- cairo
- pango-1.0
- pangocairo-1.0
- xkbcommon
- glib-2.0
- gobject-2.0
- wayland-protocols
- wayland-scanner

The build uses GNU Make and generates private C bindings from the installed
Wayland protocol XML files. The layer-shell XML is bundled because it is
distributed separately by wlr-protocols.

## Installation

```sh
make
sudo make install
```

## Nix

Add `dmenu-wl` as a flake input and install it with Home Manager:

```nix
inputs.dmenu-wl.url = "github:miksa234/dmenu-wl";

home.packages = [
  inputs.dmenu-wl.packages.${pkgs.system}.default
];
```

Or install the package directly:

```sh
nix shell github:miksa234/dmenu-wl
```

## Running dmenu-wl ...

### ... as the application launcher in Sway

Add to sway configuration (`~/.config/sway/config`) to run the launcher on Win+D.

    bindsym $mod+d exec dmenu-wl_run -i

### ... from the command-line

See the man page for details.

```
Usage: dmenu-wl [OPTION]...

Display newline-separated input stdin as a centered menu

    -e,  --echo                       display text from stdin with no user
                                      interaction
    -ec, --echo-centre                same as -e but align text centrally
    -er, --echo-right                 same as -e but align text right
    -et, --echo-timeout SECS          close the message after SEC seconds
                                      when using -e, -ec, or -er
    -c,  --center                     dmenu appears as a centered menu
    -b,  --bottom                     dmenu appears as a full-width bottom bar
    -t,  --top                        dmenu appears as a full-width top bar
    -h,  --height N                   set dmenu to be N pixels high
    -i,  --insensitive                dmenu matches menu items case insensitively
    -l,  --lines LINES                dmenu lists items vertically, within the
                                      given number of lines
    -mw, --min-width N                minimum centered-menu width (default 600)
    -bw, --border-width N             border width in logical pixels (default 3)
    -bc, --border-color COLOR         border color
    -m,  --monitor MONITOR            dmenu appears on the given monitor
                                      (0-based index or monitor name)
    -p,  --prompt  PROMPT             prompt to be displayed to the left of the
                                      input field
    -po, --prompt-only  PROMPT        same as -p but don't wait for stdin
                                      useful for a prompt with no menu
    -r,  --return-early               return as soon as a single match is found
    -fn, --font-name FONT             font or font set to be used
    -nb, --normal-background COLOR    normal background color
                                      #RRGGBB and #RRGGBBAA supported
    -nf, --normal-foreground COLOR    normal foreground color
    -sb, --selected-background COLOR  selected background color
    -sf, --selected-foreground COLOR  selected foreground color
    -v,  --version                    display version information
```
