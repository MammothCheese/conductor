#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# CONDUCTOR's host tests (no hardware), from Dinghy's, for the FM-1 only (CONDUCTOR is not made for the SMK-37;
# Dinghy's SMK-37 tests are in its history). Run from the repo root after building:
#   sh build.sh; sh tests/run_tests.sh
# The app on the host (tests/run_host.sh, tests/scripts), the update path (ota.c, the loader, tools/fm1_install.py)
# and the package.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # (fm1pkg_make.py: the SDK files of a package)
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f build/conductor.fwsc ] || { echo "run ./build.sh first"; exit 1; }

run "FM-1 app on the host: the default script plays, screens and sound" sh tests/run_host.sh
run "FM-1 MIDI: USB, the TRS jack, a clock, the channel" sh tests/run_host.sh tests/scripts/midi.txt
run "CONDUCTOR: the orchestra, the setups, the piano roll, the music, the hall" sh tests/run_host.sh tests/scripts/conductor.txt
run "CONDUCTOR: writing notes and hits, the save, REC held" sh tests/run_host.sh tests/scripts/conductor_edit.txt
run "CONDUCTOR: expression, balance, MIX, the curves" sh tests/run_host.sh tests/scripts/conductor_expression.txt
run "CONDUCTOR: recorded expression (REC, PLAY, KNOB 1), its curve" sh tests/run_host.sh tests/scripts/conductor_xrec.txt
run "CONDUCTOR: ARP, the setups a part a page, PRESETS, 64 steps, SEQ the Composer" sh tests/run_host.sh tests/scripts/conductor_controls.txt
run "CONDUCTOR: REC held asks (a big card), the music page playing (its line), the hall's rooms" sh tests/run_host.sh tests/scripts/conductor_hall_music.txt
run "CONDUCTOR: live recording on the grid (the roll, the music)" sh tests/run_host.sh tests/scripts/conductor_liverec.txt
run "CONDUCTOR: the drum machine's slots (ALGORITHM the sound: snare 2, toms, cymbal 1 / 2), OCT the page" sh tests/run_host.sh tests/scripts/conductor_kit.txt
run "CONDUCTOR: the copy page (SEQ), OCT held transposes, the length in bars, expression by marking" sh tests/run_host.sh tests/scripts/conductor_copy.txt
run "CONDUCTOR: the song page (SELECT, REC): 3/4, articulations (ALGORITHM) on the roll and the music" sh tests/run_host.sh tests/scripts/conductor_song.txt
run "CONDUCTOR: playing dots, a part's page (expression, vibrato, balance, patterns), MIX plays, the song page" sh tests/run_host.sh tests/scripts/conductor_parts.txt
run "CONDUCTOR: recording over two patterns (the next waits, takes the length)" sh tests/run_host.sh tests/scripts/conductor_recnext.txt
run "CONDUCTOR: pizzicato (the strings only): the roll, the music, not for the winds" sh tests/run_host.sh tests/scripts/conductor_pizz.txt
run "CONDUCTOR: a part's settings a pattern (expression, vibrato, balance, players): B its own, A kept" sh tests/run_host.sh tests/scripts/conductor_patset.txt
run "CONDUCTOR: time signatures (2/4 3/4 4/4 5/4 2/2 3/8 6/8 12/8): 6/8, its roll and music" sh tests/run_host.sh tests/scripts/conductor_meters.txt
run "CONDUCTOR: ARP + SELECT, a beat a click (the roll, the music), no rest on letting go" sh tests/run_host.sh tests/scripts/conductor_beatjump.txt
run "CONDUCTOR: live transpose (KNOB 3), nudge (ARP + KNOB 4), undo (ARP + REC), hairpins, the click and count-in, swing, humanise, tap tempo" sh tests/run_host.sh tests/scripts/conductor_tools.txt
run "CONDUCTOR: clear all (REC held, REC again): swing and humanise off, the mix in the middle (mf, 0 dB, concert hall, metronome off), sections, normal vibrato, nothing muted" sh tests/run_host.sh tests/scripts/conductor_clear.txt
run "CONDUCTOR: the KEY page (D major, the music moved, scale lock, the keys lit), trill and tremolo, con sordino, listen alone, the song ending" sh tests/run_host.sh tests/scripts/conductor_key.txt
run "CONDUCTOR: orchestral terms (tutti, solo, tacet; PERFORMANCE and the tempo marking; PIECE; SECTIONAL; FINE - FERMATA; K3 dynamic)" sh tests/run_host.sh tests/scripts/conductor_terms.txt
run "CONDUCTOR: ARP says what each player setting means (tutti: whole section, solo: one player, tacet: none); the curve footer" sh tests/run_host.sh tests/scripts/conductor_players.txt

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (FM-1)" "$OUT/ota_test" build/conductor.fwsc
head -c 60000 build/conductor.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_screen_test" tests/ldr_screen_test.c
run "update loader's screen (from Skiff): UPDATE MODE, WRITING and its bar, UPDATE FAILED" "$OUT/ldr_screen_test" "$OUT"
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader (FM-1): other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/conductor.fwsc

run "installer (tools/fm1_install.py): install and the update-mode resume check the model" python3 tests/install_test.py

[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$fail"
