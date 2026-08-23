{ pkgs }:

pkgs.stdenvNoCC.mkDerivation {
  pname = "llvm-toolchain";
  version = "23.1.0-rc3";

  src = pkgs.fetchurl {
    url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.0-rc3/LLVM-23.1.0-rc3-macOS-ARM64.tar.xz";
    hash = "sha256-0j3AuvKSJeKXVEfym7yYtPw3eQAeka3EpdfZWFRoHnk=";
  };

  installPhase = ''
    runHook preInstall

    mkdir -p "$out/bin" "$out/lib"

    # Keep only the compiler, linker, archive tools, and Clang tools used by
    # libuc. Dereferencing avoids retaining links to omitted tools.
    for tool in \
      clang \
      clang-format \
      clang-tidy \
      clangd \
      ld.lld \
      llvm-ar \
      llvm-strip
    do
      install -m755 "bin/$tool" "$out/bin/$tool"
    done
    ln -s clang "$out/bin/clang++"
    ln -s llvm-ar "$out/bin/llvm-ranlib"

    # Clang's freestanding headers and sanitizer runtimes live in its resource
    # directory. The selected executables are otherwise self-contained and
    # link only against libraries supplied by macOS.
    cp -R lib/clang "$out/lib/"

    runHook postInstall
  '';

  # Preserve the upstream Mach-O load commands and signatures.
  dontFixup = true;

  meta = {
    description = "Reduced official LLVM 23.1.0-rc3 toolchain for libuc";
    homepage = "https://llvm.org/";
    license = pkgs.lib.licenses.asl20;
    platforms = [ "aarch64-darwin" ];
  };
}
