{
  stdenv,
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
    make install PREFIX="$out"
  '';
}
