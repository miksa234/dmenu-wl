{
  description = "dmenu-wayland development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in {
      shell = pkgs.zsh;

      devShells.${system}.default = pkgs.mkShell {
        # Compiler and build tools
        nativeBuildInputs = with pkgs; [
          gcc
          gnumake
          pkg-config
          wayland-scanner
        ];

        # Libraries, protocols, and headers
        buildInputs = with pkgs; [
          cairo
          glib
          libxkbcommon
          pango
          wayland
          wayland-protocols
        ];
      };
    };
}
