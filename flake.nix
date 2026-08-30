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
      # clang-unwrapped already bundles the raw clang-tools-extra binaries;
      # clang-tools would collide with them in buildEnv, so only the
      # devshell layers the wrapped ones on top for editor tooling.
      toolchain =
        pkgs:
        let
          llvm = pkgs.llvmPackages_22;
        in
        [
          pkgs.cpio
          pkgs.gawk
          pkgs.gzip
          pkgs.meson
          pkgs.ninja
          llvm.clang-unwrapped
          llvm.lld
          llvm.llvm
        ];
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          toolchain = pkgs.buildEnv {
            name = "libuc-toolchain";
            paths = toolchain pkgs;
          };
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShellNoCC {
            name = "libuc";
            packages = [ pkgs.llvmPackages_22.clang-tools ] ++ toolchain pkgs;
          };
        }
      );
    };
}
