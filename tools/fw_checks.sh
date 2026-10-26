#!/bin/bash
# Static checks of the stage 1 firmware integration (tasks E2.2 to E2.8 and E3.3),
# on the release build and on the sources: symbols that must or must not be linked,
# calls that a function must or must not make, what sits in ITCM, and source patterns.
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
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
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
# function $1 branches to nothing but itself and the functions $2...
only_calls()  { local f="$1" t s ok; shift; linked "$f" || { fail "$f is not linked"; return; }
                for t in $(calls_of "$f" | sort -u); do
                    ok=0; for s in "$f" "$@"; do grep -qE "^(__)?$s(\\.[a-z]+\\.[0-9]+)*(_veneer)?\$" <<<"$t" && ok=1; done
                    [ $ok -eq 1 ] || fail "$f calls $t"
                done; }
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
	# the Note Ons the core does not schedule, the one-shot roll and the echo of an incoming
	# note, get their Note Off one pulse later from seq_tick(), as core events do (E4.1 F22,
	# spec 12: no hanging notes)
	need seq_sendVoiceNoteOffLater
	must_call midiParser_noteOn seq_sendVoiceNoteOffLater
	awk '{sub(/\r$/, "")} /^void seq_triggerVoice\(/{f=1} f&&/^}/{exit}
		f&&/^[[:space:]]+seq_sendVoiceNoteOn\(voiceNr,/{on=1} f&&on&&/^[[:space:]]+seq_sendVoiceNoteOffLater\(voiceNr\);/{ok=1}
		END{exit !ok}' Core/Src/Soundengine/Sequencer/sequencer.c \
		|| fail "seq_triggerVoice() does not time the Note Off of its Note On"
	awk '{sub(/\r$/, "")} /^void seq_tick\(\)/{f=1} f&&/^}/{exit}
		f&&/^[[:space:]]+seqc_process\(now\);/{p=1} f&&p&&/seq_sendDueNoteOffs\(now\)/{ok=1}
		END{exit !ok}' Core/Src/Soundengine/Sequencer/sequencer.c \
		|| fail "seq_tick() does not send the timed Note Offs after seqc_process()"
	awk '{sub(/\r$/, "")} /^void seq_sendVoiceNoteOff\(/{f=1} f&&/^}/{exit}
		f&&/seq_voiceOffPending &= /{c=1} f&&/midi_voicesOn & /{if(!c)bad=1}
		END{exit !(c && !bad)}' Core/Src/Soundengine/Sequencer/sequencer.c \
		|| fail "seq_sendVoiceNoteOff() does not cancel the timed Note Off of the voice first"
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
	# the handling of an entry can call out of the sequencer for long (a kit load), and the
	# entries stamped meanwhile must not be popped against the time read before it: the
	# drain reads the time again before every pop, drains to the end, and seqc_process()
	# gets the time of its last pop (E2.4 review). Two checks: the read sits in the loop
	# (the non-blank lines "for(;;)", "{", the read, the pop and "break;" follow each
	# other, and the function pops in that place only), and the drain runs until the
	# queues are empty (the break after the pop and the final "return drained;" are the
	# only break, return or goto in the function, N21). drain_lines prints the
	# function's lines trimmed, without blank lines and whole-line // comments; in the CRLF
	# file a blank line is a lone \r, one field for NF: strip it first
	drain_lines() {
		awk '{sub(/\r$/, "")} /^static uint8_t seq_drainSyncQueues\(seqc_time_t \*now\)/{f=1}
			f&&/^}/{exit} f&&NF{sub(/^[[:space:]]+/, ""); sub(/[[:space:]]+$/, ""); if(!/^\/\//)print}' \
			Core/Src/Soundengine/Sequencer/sequencer.c
	}
	drain_lines | awk '{l[NR]=$0} /seqp_syncPop\(/{pops++}
		END{for(i=1; i+4<=NR; i++) if(l[i]=="for(;;)" && l[i+1]=="{" && l[i+2]=="*now = seqp_now();" &&
			l[i+3] ~ /^if\(!seqp_syncPop\(\*now, / && l[i+4]=="break;") ok=1
			exit !(ok && pops==1)}' \
		|| fail "seq_drainSyncQueues() does not read the time before every pop"
	drain_lines | awk '/return drained;/{n++} /(^|[^[:alnum:]_])(break|return|goto)([^[:alnum:]_]|$)/{exits++}
		END{exit !(n==1 && exits<=2)}' \
		|| fail "seq_drainSyncQueues() stops before the queues are empty"
	# a Program Change switch while stopped rewinds through the mailbox, applied in the
	# drain before the next entry, not at the next seq_tick() (E2.4 review)
	drain_lines | awk '{l[NR]=$0} END{for(i=1; i+2<=NR; i++) if(l[i]=="seq_setNextPattern((uint8_t)entry.value);" &&
			l[i+1]=="if(seq_mailboxPosted)" && l[i+2]=="seq_applyMailbox();") ok=1
			exit !ok}' \
		|| fail "seq_drainSyncQueues() does not apply the mailbox after a Program Change switch"
	# the drain's gates and its SPP dispatch (E4.1 F30): a Program Change switches only with
	# the fw 1.7 filter and on the global channel; the RX filter for system messages drops
	# a DIN or USB entry before the SPP branch and before seqc_syncEvent(), not the jack's;
	# an SPP goes to seqc_songPosition() and nowhere else
	drain_lines | awk '$0=="if((midiParser_txRxFilter & MIDI_FILTER_PROG_C) && ((entry.value >> 8) == midi_MidiChannels[7]))"{n++}
		END{exit !(n==1)}' \
		|| fail "seq_drainSyncQueues() does not filter a Program Change as fw 1.7 did"
	drain_lines | awk '{l[NR]=$0}
		END{for(i=1; i+1<=NR; i++) if(l[i]=="if((source != SEQP_SRC_JACK) && !(midiParser_txRxFilter & MIDI_FILTER_SYSTEM))" &&
			l[i+1]=="continue;") g=i
			for(i=1; i+4<=NR; i++) if(l[i]=="if(entry.type == SEQQ_SONG_POSITION)" && l[i+1]=="{" &&
			l[i+2]=="seqc_songPosition(entry.value);" && l[i+3]=="continue;" && l[i+4]=="}") s=i
			for(i=1; i<=NR; i++) if(l[i] ~ /^seqc_syncEvent\(/) e=i
			exit !(g && s && e && g<s && s<e)}' \
		|| fail "seq_drainSyncQueues() does not gate system messages with the RX filter before the SPP and the sync events"
	must_call seq_tick seqc_songPosition
	# an SPP deep into a looping song walks from one period before its own, not entry by
	# entry from step 0 (E4.1 F33: up to 16384 entries in seq_tick()); the host tests check
	# the same walk in tests/fake_host.c against the full walk (harness_seek_*). Here: the
	# period is skipped, and after walkStep is set the walk never reads absStep again
	awk '{sub(/\r$/, "")} /^static uint32_t seq_cbSeekPosition\(/{f=1} f&&/^}/{exit}
		f&&$0=="\tconst uint32_t skipped = (period && (absStep >= 2 * period)) ? (absStep / period - 1) * period : 0;"{k=1}
		f&&k&&$0=="\tconst uint32_t walkStep = absStep - skipped;"{w=1; next} f&&w&&/absStep/{bad=1}
		f&&w&&/^[[:space:]]+if\(walkStep < entryStart \+ len\)$/{ok=1} END{exit !(ok && !bad)}' \
		Core/Src/Soundengine/Sequencer/sequencer.c || fail "seq_cbSeekPosition() walks a looping song from step 0"
	awk '{sub(/\r$/, "")} /^void seq_tick\(\)/{f=1} f&&/seq_drainSyncQueues\(&now\)/{d=1;next} f&&d&&NF{if(/^[[:space:]]+changed = 1;$/)next; if(/^[[:space:]]+seqc_process\(now\);$/)ok=1; d=0}
		f&&/^}/{exit} END{exit !ok}' \
		Core/Src/Soundengine/Sequencer/sequencer.c || fail "seq_tick() does not pass the time of the last pop to seqc_process()"
	# the order after the drain (E4.1 F36): seqc_process(), then the copies for the UI,
	# then the MIDI parse as the last statement inside the nesting guard, so that a note
	# recorded there sees the phase and the step copies of this call
	awk '{sub(/\r$/, "")} /^void seq_tick\(\)/{f=1; next} f&&/^}/{exit}
		f&&NF{sub(/^[[:space:]]+/, ""); sub(/[[:space:]]+$/, ""); if(!/^\/\//)l[++n]=$0}
		END{for(i=1; i<=n; i++){if(l[i]=="seqc_process(now);"){p++; pi=i} if(l[i]=="seq_refreshCopies(changed);"){r++; ri=i}
			if(l[i]=="seq_parseMidiInput();"){m++; mi=i}}
			exit !(p==1 && r==1 && m==1 && pi<ri && ri<mi && l[mi+1]=="seq_inTick = 0;" && mi+1==n)}' \
		Core/Src/Soundengine/Sequencer/sequencer.c \
		|| fail "seq_tick() does not run seqc_process(), then seq_refreshCopies(), then seq_parseMidiInput() last"
}

# ---- E2.5: clock in and reset in jacks through the jack sync queue ----------
check_E2_5() {
	must_call EXTI4_IRQHandler seqp_jackIn
	must_call EXTI9_5_IRQHandler seqp_jackIn
	must_not_call EXTI4_IRQHandler seq_setBpm
	must_not_call EXTI9_5_IRQHandler seq_setRunning seq_resetPosition
	need seq_setClockInPpq
	gone trigger_clockReceived trigger_calcBpm trigger_timeLast trigger_clockCnt syncTrigger_writePos \
		trigger_ResetPosition trigger_prescaleCounterClockInput
	# trigger_calcBpm and trigger_ResetPosition were inlined in fw 1.7 already: gone cannot see them
	no_source 'trigger_calcBpm|trigger_ResetPosition|HANDLE_SEQ_IN_CLOCK_INTERRUPT' \
		Core/Src/Soundengine/TriggerOut.c Core/Inc/Soundengine/TriggerOut.h
	# the interrupts only stamp: the sync mode test and the push, nothing else
	must_call EXTI4_IRQHandler seq_getExtSync
	must_call EXTI9_5_IRQHandler seq_getExtSync
	only_calls EXTI4_IRQHandler seq_getExtSync seqp_jackIn
	only_calls EXTI9_5_IRQHandler seq_getExtSync seqp_jackIn
	# the event types, and the run gate's polarity: a low pin is an active reset
	has_source 'seqp_jackIn\(SEQC_EV_PULSE, stamp\);' Core/Src/Soundengine/TriggerOut.c
	has_source 'seqp_jackIn\(pinState \? SEQC_EV_RESET_OFF : SEQC_EV_RESET_ON, stamp\);' Core/Src/Soundengine/TriggerOut.c
	# the reset handler clears the pending bit before it reads the pin, never after: a
	# clear after the read could lose an edge and leave the gate stuck
	awk '/^void EXTI9_5_IRQHandler\(void\)/{f=1} f&&/->IDR/{r=1} f&&r&&/__HAL_GPIO_EXTI_CLEAR_IT/{bad=1} f&&/^}/{exit} END{exit bad}' \
		Core/Src/Soundengine/TriggerOut.c || fail "EXTI9_5_IRQHandler clears the pending bit after it reads the pin"
	# menu value 0 is 4 PPQ and 1 is 24 PPQ (ppqInNames)
	has_source 'seq_setClockInPpq\(trigger_prescalerClockInput == PRE_24_PPQ_IN \? 24 : 4\);' Core/Src/Soundengine/MIDI/CortexParser.c
	# fw 1.7 sent all notes off on every active gate edge, also where the core reports no rewind
	has_source 'entry\.type == SEQC_EV_RESET_ON\) && \(seq_coreSyncMode == SEQC_SYNC_ANALOG\)\)' \
		Core/Src/Soundengine/Sequencer/sequencer.c
}

# ---- E2.6: clockSync and the dead timing code are gone; ITCM holds no sequencer code
check_E2_6() {
	no_file Core/Src/Soundengine/Sequencer/clockSync.c Core/Inc/Soundengine/Sequencer/clockSync.h
	no_tree 'clockSync|seq_MidiSyncQueue|seq_getDeltaTShuffleDelay|seq_getMasterStepCnt|seq_prescaleCounter|seq_sync\(|midi_process_block|HANDLE_SEQ_IN_CLOCK_INTERRUPT|trigger_clockReceived' \
		Core/Src/Soundengine Core/Inc/Soundengine Core/CMakeLists.txt
	no_source 'seq_newPatternAvailable' Core/Inc/Soundengine/Sequencer/sequencer.h
	no_source 'medianFilter' Core/Src/Soundengine/TriggerOut.c
	sed -n '/^set(LXR_ITCM_SOURCES/,/^)/p' Core/CMakeLists.txt | grep -q 'sequencer\.c' \
		&& fail "sequencer.c is still in LXR_ITCM_SOURCES"
	no_itcm 'seq'
	gone sync_tick sync_window sync_timeLast sync_clockCnt sync_writePos sync_delayedStepTimer
}

# ---- E2.7: the global menu has a button mode of its own (SELECT_MODE_MENU) ---
check_E2_7() {
	no_source '^[^/]*selectButtonMode[[:space:]]*=[[:space:]]*SEQ_' Core/Src/buttonHandler.c
	sed -n '/case SEQ_CONFIG:/,/break;/p' Core/Src/buttonHandler.c | grep -q 'selectButtonMode = SELECT_MODE_MENU;' \
		|| fail "case SEQ_CONFIG (SHIFT + step 12) does not set SELECT_MODE_MENU"
}

# ---- E2.8: USB MIDI out: packets with their CIN, a ring that waits for the endpoint
check_E2_8() {
	need usbtx_packet usbtx_push usbtx_next
	must_call usb_sendMidi usbtx_packet usbtx_push usbtx_next HAL_GetTick
	must_call usb_midiTxComplete usbtx_next HAL_GetTick CDC_Transmit_FS
	must_call USBD_CDC_DataIn usb_midiTxComplete
	gone usb_MidiInBuff usb_MidiInWrPtr usb_MidiInRdPtr
	no_tree 'usb_MidiIn|usb_sendByte|TOTAL_MIDI_BUF_SIZE|NUM_SUB_BUFFERS' Core/Src Core/Inc Middlewares/JS
	grep -A1 'case MIDI_SONG_SEL:' Core/Src/Soundengine/MIDI/MidiParser.c | grep -q 'parserState = MIDI_DATA1' \
		|| fail "the MIDI parser routes a Song Select before its data byte"
	# a system common message without data (F4, F5, F6) is routed alone, not with the data
	# bytes of the message before it (fw 1.7 sent a Tune Request after a Note On as F6 3C 64)
	awk '{sub(/\r$/, "")} /^[[:space:]]+default: \/\* 0 byte payload messages/{f=1} f&&/^[[:space:]]+midiMsg_tmp\.bits\.length = 0;/{ok=1}
		f&&/break;/{exit} END{exit !ok}' Core/Src/Soundengine/MIDI/MidiParser.c \
		|| fail "the MIDI parser routes F4, F5 and F6 with the data bytes of the message before"
	# USBD_CDC_DataOut() (OTG_FS) drops a message when the USB input queue is full, which is
	# safe only if usb_getMidi() reads the write index before the slot, and copies the slot
	# before it frees it: the fences pin that order, in this order (E2.4 review)
	awk '{sub(/\r$/, "")} /^uint8_t usb_getMidi\(MidiMsg\* msg\)/{f=1} f&&/^}/{exit}
		f&&/atomic_signal_fence\(memory_order_acquire\)/{a=1}
		f&&/\*msg = usb_MidiMessages\[/{if(a)c=1}
		f&&/atomic_signal_fence\(memory_order_release\)/{if(c)r=1}
		f&&/^[[:space:]]+usb_MidiMessagesRead[[:space:]]*(=|\+\+|&=)/{if(r)ok=1; else bad=1}
		END{exit !(ok && !bad)}' Core/Src/Soundengine/USB/usb_manager.c \
		|| fail "usb_getMidi() does not order the index, the copy and the freed slot"
}

# ---- E3.3: the CPU meter; its test hook stays out of the firmware ------------
check_E3_3() {
	need cpuMeter_begin cpuMeter_end cpuMeter_get
	# cpuMeter_reset() exists for the host tests only (CPUMETER_TEST, E4.1 F34): the
	# firmware object must not have it, not merely the image after --gc-sections
	local obj="$ROOT/build/release/Core/CMakeFiles/lxr_engine.dir/Src/Soundengine/cpuMeter.c.obj"
	exists "$obj"
	[ -e "$obj" ] && arm-none-eabi-nm "$obj" | grep -qw cpuMeter_reset && fail "cpuMeter.c.obj has the test hook cpuMeter_reset"
}

check_tasks="E2.2 E2.3 E2.4 E2.5 E2.6 E2.7 E2.8 E3.3"

if [ $# -gt 0 ]; then check_tasks="$*"; fi
for task in $check_tasks; do
	fn="check_${task//./_}"
	if ! declare -F "$fn" >/dev/null; then echo "unknown task $task"; exit 2; fi
	task_failed=0
	"$fn"
	[ $task_failed -eq 0 ] && echo "PASS $task"
done
exit $failed
