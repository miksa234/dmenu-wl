{
  stdenv,
  gcc,
  gnumake,
  makeWrapper,
  pkg-config,
  wayland-scanner,
  cairo,
  glib,
  libxkbcommon,
  pango,
  wayland,
  wayland-protocols,
  ...
}:

stdenv.mkDerivation {
  pname = "dmenu-wl";
  version = "5.0.0";
  src = ../.;

  nativeBuildInputs = [
    gcc
    gnumake
    makeWrapper
    pkg-config
    wayland-scanner
  ];
  buildInputs = [
    cairo
    glib
    libxkbcommon
    pango
    wayland
    wayland-protocols
  ];

  installPhase = ''
    mkdir -p "$out/bin" "$out/share/man/man1"
    install -m755 build/dmenu-wl "$out/bin/dmenu-wl"
    install -m755 build/dmenu-wl_path "$out/bin/dmenu-wl_path"
    install -m755 dmenu-wl_run "$out/bin/dmenu-wl_run-unwrapped"
    sed 's/VERSION/5.0.0/g' dmenu-wl.1 > "$out/share/man/man1/dmenu-wl.1"
    chmod 644 "$out/share/man/man1/dmenu-wl.1"
    makeWrapper "$out/bin/dmenu-wl_run-unwrapped" "$out/bin/dmenu-wl_run" \
      --prefix PATH : "$out/bin"
  '';
}
