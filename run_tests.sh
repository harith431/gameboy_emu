#!/usr/bin/env bash
# Game Boy emulator test runner.
#
# Runs the blargg + mooneye test ROMs currently in test_roms/ and prints a
# PASS/FAIL summary. Pass/fail is detected from the serial output:
#   * mooneye: PASS = bytes 03 05 08 0D 15 22, FAIL = 42 x6 ("BBBBBB")
#   * blargg : PASS = ASCII "Passed", FAIL = "Failed"
#
# Usage: ./run_tests.sh
set -u
cd "$(dirname "$0")"

BIN=./build/gameboy_emu.exe
export PATH="/c/msys64/ucrt64/bin:$PATH"

if [ ! -x "$BIN" ]; then
  echo "error: $BIN not found — build first (see README.md)" >&2
  exit 1
fi

PASS=0
FAIL=0
FAILED_TESTS=()

run() {
  local rom="$1" cycles="${2:-300000000}"
  timeout 60 "$BIN" "$rom" --headless --cycles "$cycles" 2>/dev/null > /tmp/gbemu_test_out.bin
  if grep -q $'\x42\x42\x42\x42\x42\x42' /tmp/gbemu_test_out.bin; then
    echo "FAIL  $rom"; FAIL=$((FAIL+1)); FAILED_TESTS+=("$rom")
  elif grep -q $'\x03\x05\x08\x0d\x15\x22' /tmp/gbemu_test_out.bin; then
    echo "PASS  $rom"; PASS=$((PASS+1))
  elif grep -q "Passed" /tmp/gbemu_test_out.bin; then
    echo "PASS  $rom (blargg)"; PASS=$((PASS+1))
  else
    echo "????  $rom (no recognizable output)"; FAIL=$((FAIL+1)); FAILED_TESTS+=("$rom")
  fi
}

echo "===== blargg CPU ====="
for i in 01 02 03 04 05 06 07 08 09 10 11; do run "test_roms/cpu_$i.gb" 400000000; done
run test_roms/cpu_instrs.gb 600000000
run test_roms/instr_timing.gb 200000000

echo "===== mooneye timer ====="
for t in div_timing div_write tim00 tim01 tim10 tim11 tima_reload tima_write_reloading tma_write_reloading rapid_toggle; do
  run "test_roms/mooneye/$t.gb"
done

echo "===== mooneye MBC1 ====="
for t in bits_bank1 bits_bank2 bits_mode bits_ramg rom_512kb rom_1Mb rom_2Mb rom_4Mb rom_8Mb rom_16Mb ram_64kb ram_256kb; do
  run "test_roms/mooneye/mbc1/$t.gb"
done

echo "===== mooneye MBC2 ====="
for t in bits_ramg bits_romb bits_unused ram rom_512kb rom_1Mb rom_2Mb; do
  run "test_roms/mooneye/mbc2/$t.gb"
done

echo "===== mooneye MBC5 ====="
for t in rom_512kb rom_1Mb rom_2Mb rom_4Mb rom_8Mb rom_16Mb rom_32Mb rom_64Mb; do
  run "test_roms/mooneye/mbc5/$t.gb"
done

echo "=================================="
echo "PASS=$PASS FAIL=$FAIL"
if [ "$FAIL" -ne 0 ]; then
  echo "Failed tests:"
  printf '  %s\n' "${FAILED_TESTS[@]}"
  exit 1
fi
exit 0
