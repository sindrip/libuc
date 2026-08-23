{
  description = "Development tools for libuc";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "aarch64-darwin"
        "aarch64-linux"
        "x86_64-darwin"
        "x86_64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages.aarch64-darwin.llvm23-rc3 =
        let
          pkgs = import nixpkgs { system = "aarch64-darwin"; };
        in
        import ./llvm23-rc3.nix { inherit pkgs; };

      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          llvm = pkgs.llvmPackages_22;
        in
        {
          default = pkgs.mkShellNoCC {
            name = "libuc";
            packages = [
              pkgs.cpio
              pkgs.gzip
              pkgs.meson
              pkgs.ninja
              llvm.clang-tools
              llvm.clang-unwrapped
              llvm.lld
              llvm.llvm
            ];
          };
        }
      );
    };
}
