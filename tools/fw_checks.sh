#!/bin/bash
# Static checks of the stage 1 firmware integration (tasks E2.2 to E2.6), on the
# release build and on the sources: symbols that must or must not be linked, calls
# that a function must or must not make, what sits in ITCM, and source patterns.
# They stand in for unit tests where the code needs the hardware.
#
#   tools/check.sh             first: it builds build/release
#   tools/fw_checks.sh         every check
#   tools/fw_checks.sh E2.3    the checks of one task
#
# Prints one line per failed check and "PASS <task>" per task without failures.
# The exit status is 1 if a check failed.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
set -u -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1
# shellcheck source=env.sh
source "$ROOT/tools/env.sh"
ELF="$ROOT/build/release/LXR_V2_STM32F765.elf"
[ -f "$ELF" ] || { echo "no $ELF: run tools/check.sh first"; exit 1; }

SYMS="$(arm-none-eabi-nm "$ELF" | awk '{print $NF}' | sort -u)"
ITCM="$(arm-none-eabi-objdump -t "$ELF" | grep -F '.itcm_text' | awk '{print $NF}')"
task=""
failed=0
task_failed=0

fail() { echo "FAIL $task: $*"; failed=1; task_failed=1; }

# the symbol names of a function: the name and its compiler clones (name.part.0, ...)
names() { grep -E "^$1(\\.[a-z]+\\.[0-9]+)*\$" <<<"$SYMS"; }
linked() { [ -n "$(names "$1")" ]; }
# the targets that function $1 branches to, from its disassembly (all its clones)
calls_of() {
	local n
	for n in $(names "$1"); do
		arm-none-eabi-objdump -d --disassemble="$n" "$ELF" | grep -oE '<[^>+]+>' | tr -d '<>'
	done
}
# does function $1 call $2 (directly, through a veneer or a clone)? The targets are
# captured first: grep -q on a pipe would stop reading after the first match and the
# objdump of a second clone would die of SIGPIPE, which pipefail turns into "no call"
calls() { local targets; targets="$(calls_of "$1")"
          grep -qE "^(__)?$2(\\.[a-z]+\\.[0-9]+)*(_veneer)?\$" <<<"$targets"; }

need()        { local s; for s in "$@"; do linked "$s" || fail "$s is not linked"; done; }
gone()        { local s; for s in "$@"; do linked "$s" && fail "$s is still linked"; done; }
must_call()   { local f="$1" s; shift; linked "$f" || { fail "$f is not linked"; return; }
                for s in "$@"; do calls "$f" "$s" || fail "$f does not call $s"; done; }
must_not_call() { local f="$1" s; shift; linked "$f" || { fail "$f is not linked"; return; }
                for s in "$@"; do calls "$f" "$s" && fail "$f still calls $s"; done; }
no_itcm()     { local s; for s in $(grep -E "^$1" <<<"$ITCM"); do fail "$s is in ITCM"; done; }
# every path must exist: grep exits 2 on a missing one, which would pass silently
exists()      { local p; for p in "$@"; do [ -e "$p" ] || fail "$p does not exist"; done; }
no_source()   { local pattern="$1" hits; shift; exists "$@"
                hits="$(grep -nE "$pattern" "$@" 2>/dev/null)" && fail "'$pattern' found: $(head -n 3 <<<"$hits" | tr -d '\r' | paste -sd ' ' -)"; }
no_file()     { local f; for f in "$@"; do [ -e "$f" ] && fail "$f still exists"; done; }
no_tree()     { local pattern="$1" hits; shift; exists "$@"
                hits="$(grep -rnE "$pattern" "$@" 2>/dev/null)" && fail "'$pattern' found: $(head -n 3 <<<"$hits" | tr -d '\r' | paste -sd ' ' -)"; }
has_source()  { local pattern="$1"; shift
                grep -qE "$pattern" "$@" 2>/dev/null || fail "'$pattern' not found in $*"; }

# ---- E2.2: seq_core drives the sequencer, the pulse machine is gone ----------
check_E2_2() {
	need seqc_init seqc_process seqp_init seqp_now seqp_syncPop seq_tick seq_cbTransport seq_cbTrigger \
		seq_cbPatternEnd seq_cbStepBoundary
	must_call seq_tick seqc_process seqp_now
	gone seq_processFlam seq_getShiftFlamPattern seq_nextStep seq_MidiSyncQueue seq_triggerNextMasterStep \
		seq_setDeltaT sync_tick chokeCurrent chokeNext lastWasNoteOn isStepShuffled seq_stepIndex seq_deltaT \
		seq_lastTick seq_rndValue seq_lastShuffle seq_masterStepCnt seq_stepToSkip seq_lastMasterStep
	no_itcm 'seq_'
	no_source 'IN_ITCM_RAM' Core/Src/Soundengine/Sequencer/sequencer.c
	# N18.2: MIDI Start, Stop and Continue go out only as master, through seq_sendTransport()
	has_source 'seq_coreSyncMode == SEQC_SYNC_INTERNAL' Core/Src/Soundengine/Sequencer/sequencer.c
	no_source 'seq_sendRealtime\(MIDI_(STOP|START|CONTINUE)\)' Core/Src/Soundengine/Sequencer/sequencer.c
	# the clock out divider follows the position, so a Continue inside a step stays on the grid
	must_call seq_cbClockOutPulse trigger_setClockPhase trigger_clockTick
}

# ---- E2.3: one MIDI Note On per trigger, Note Offs per voice (D4) ------------
check_E2_3() {
	need seq_sendVoiceNoteOn seq_sendVoiceNoteOff
	must_not_call voiceControl_noteOn seq_sendMidiNoteOn seq_sendVoiceNoteOn
	must_call voiceControl_noteOff seq_sendVoiceNoteOff
	must_call midiParser_noteOn seq_sendVoiceNoteOn
	must_not_call midiParser_noteOn seq_sendMidiNoteOn seq_midiNoteOff
	no_source '^[[:space:]]+seq_sendMidiNoteOn\(' Core/Src/Soundengine/Sequencer/sequencer.c \
		Core/Src/Soundengine/MIDI/MidiVoiceControl.c Core/Src/Soundengine/MIDI/MidiParser.c
	no_source 'voiceControl_noteOff\(midi_MidiChannels' Core/Src/Soundengine/Sequencer/sequencer.c
	# exactly one Note On per sequencer trigger (D4 is two)
	local n
	n=$(calls_of seq_cbTrigger | grep -cE '^seq_sendVoiceNoteOn(\.[a-z]+\.[0-9]+)*(_veneer)?$')
	[ "$n" -eq 1 ] || fail "seq_cbTrigger calls seq_sendVoiceNoteOn $n times, not once"
	# seq_triggerVoice() is inlined into seq_tick(), so check its source
	has_source '^[[:space:]]+seq_sendVoiceNoteOn\(voiceNr, midiNote, stepData' Core/Src/Soundengine/Sequencer/sequencer.c
	# all notes off (voice 0xFF) ends the Note of every voice, so no note hangs
	has_source '^[[:space:]]+seq_sendVoiceNoteOff\(i\);' Core/Src/Soundengine/MIDI/MidiVoiceControl.c
	must_call seq_setMute voiceControl_noteOff
}

# ---- E2.4: MIDI input only in seq_tick(), realtime and SPP from the ISRs (D7, D8)
check_E2_4() {
	must_call USART3_IRQHandler seqp_dinByteIn
	must_call USBD_CDC_DataOut seqp_usbMessageIn
	need uart_processMidi usb_getMidi
	must_not_call main uart_processMidi usb_getMidi midiParser_parseMidiMessage
	must_not_call soundengine_process uart_processMidi
	must_not_call TIM2_IRQHandler uart_processMidi
	gone seq_sync sync_midiResetPosition sync_midiStartStop
	no_source 'clockSync\.h' Core/Src/Soundengine/MIDI/MidiParser.c
	# only seq_tick() parses, inside its nesting guard, with the budget (E2.4 review)
	must_call seq_tick uart_processMidi usb_getMidi midiParser_parseMidiMessage
	awk '/^void seq_tick\(\)/{f=1} f&&/seq_inTick = 1;/{g=1} f&&g&&/seq_parseMidiInput\(\);/{ok=1} f&&/seq_inTick = 0;/{exit} END{exit !ok}' \
		Core/Src/Soundengine/Sequencer/sequencer.c || fail "seq_parseMidiInput() is not inside the nesting guard of seq_tick()"
	has_source '#define SEQ_DIN_BYTES_PER_TICK +32 ' Core/Src/Soundengine/Sequencer/sequencer.c
	has_source '#define SEQ_USB_MESSAGES_PER_TICK +8 ' Core/Src/Soundengine/Sequencer/sequencer.c
	# a full USB message queue drops the new message
	has_source 'USB_MIDI_INPUT_BUFFER_MASK\) == usb_MidiMessagesRead' Middlewares/JS/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c
	# realtime bytes inside a DIN message are routed alone
	has_source '^[[:space:]]+if\(data >= MIDI_CLOCK\)' Core/Src/Soundengine/MIDI/MidiParser.c
	# a Program Change switches the pattern from the sync queues, in order with the transport
	has_source 'entry\.type == SEQQ_PROGRAM_CHANGE' Core/Src/Soundengine/Sequencer/sequencer.c
	no_source 'seq_setNextPattern' Core/Src/Soundengine/MIDI/MidiParser.c
	# the drain stops after a switch while stopped and after an SPP, which can load a kit:
	# the events after them wait for a fresh now (E2.4 review)
	awk '/^static uint8_t seq_drainSyncQueues\(/{f=1} f&&/seq_applyMailbox\(\);/{a=1;next} a&&NF{if(/return drained;/)n++; a=0}
		f&&/seqc_songPosition\(entry\.value\);/{b=1;next} b&&NF{if(/return drained;/)n++; b=0} f&&/^}/{exit} END{exit n!=2}' \
		Core/Src/Soundengine/Sequencer/sequencer.c || fail "seq_drainSyncQueues() does not stop after a kit-loading Program Change or SPP"
}

check_tasks="E2.2 E2.3 E2.4"

if [ $# -gt 0 ]; then check_tasks="$*"; fi
for task in $check_tasks; do
	fn="check_${task//./_}"
	if ! declare -F "$fn" >/dev/null; then echo "unknown task $task"; exit 2; fi
	task_failed=0
	"$fn"
	[ $task_failed -eq 0 ] && echo "PASS $task"
done
exit $failed
