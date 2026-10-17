# Firmware build environment for WSL/Linux with Nix + devenv (https://devenv.sh).
#
#   devenv shell        enter the environment
#   fw-build            configure if needed, build -> build/sna_firmware.uf2
#   fw-clean            delete build/
#
# Or in one go: devenv shell fw-build
{ pkgs, inputs, ... }:

{
  packages = [
    pkgs.cmake
    pkgs.ninja
    pkgs.gcc-arm-embedded # arm-none-eabi-gcc for the RP2350
    pkgs.gcc              # host compiler: the SDK builds picotool (for the .uf2) with it
    pkgs.python3          # used by Pico SDK build steps
    pkgs.git
  ];

  env.PICO_SDK_PATH = "${inputs.pico-sdk}";
  env.PICO_TOOLCHAIN_PATH = "${pkgs.gcc-arm-embedded}";

  scripts.fw-build.exec = ''
    set -e
    cd "$DEVENV_ROOT"
    # PICO_SDK_PATH is cached: reconfigure from scratch if the cache points
    # at another SDK (e.g. after a devenv update changed the store path).
    if [ -f build/CMakeCache.txt ] &&
       ! grep -qx "PICO_SDK_PATH:[A-Z]*=$PICO_SDK_PATH" build/CMakeCache.txt; then
      echo "build uses another SDK, reconfiguring"
      rm -rf build/CMakeCache.txt build/CMakeFiles
    fi
    if [ ! -f build/CMakeCache.txt ]; then
      cmake -S . -B build -G Ninja -DPICO_SDK_PATH="$PICO_SDK_PATH"
    fi
    cmake --build build
    echo "Built build/sna_firmware.uf2"
  '';

  scripts.fw-clean.exec = ''
    rm -rf "$DEVENV_ROOT/build"
  '';

  enterShell = ''
    echo "SNA firmware: fw-build (-> build/sna_firmware.uf2), fw-clean"
    echo "Pico SDK: $PICO_SDK_PATH"
    arm-none-eabi-gcc --version | head -n 1
  '';
}
