{
  description = "dmenu-wl";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "aarch64-linux"
        "x86_64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          dmenu-wl = pkgs.callPackage ./nix/package.nix { };
        in
        {
          inherit dmenu-wl;
          default = dmenu-wl;
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              gcc
              gnumake
              pkg-config
              wayland-scanner
              bear
            ];

            buildInputs = with pkgs; [
              cairo
              glib
              libxkbcommon
              pango
              wayland
              wayland-protocols
            ];
          };
        }
      );
    };
}
