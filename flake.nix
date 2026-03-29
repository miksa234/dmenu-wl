{
  description = "dmenu-wl fork";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        shell = pkgs.zsh;
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            makeWrapper
            wayland-scanner
          ];
          buildInputs = with pkgs; [
            pkg-config
            cairo
            pango
            wayland-protocols
            glib
            wayland
            libxkbcommon
          ];
        };
      }
    );
}
