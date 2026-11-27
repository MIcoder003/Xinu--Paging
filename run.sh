#!/bin/bash
# run.sh -- build and run the ECE565 paging lab
#
# Put this at the root of the project (next to system/, include/,
# compile/) and chmod +x it.  Paths resolve from the script's own
# location, so it works from any directory.
#
# TWO SHELL WORKFLOW (recommended)
#   shell 1:  minicom -C ~/xinu.log        # leave open
#   shell 2:  ./run.sh A go                # stage, build, reset the VM
#
# ONE SHELL WORKFLOW
#   ./run.sh A                             # stage, build, open minicom
#
# Commands
#   ./run.sh A|B|C|D|E        set stage, build, open minicom
#   ./run.sh A|B|C|D|E go     set stage, build, reset the VM (no minicom)
#   ./run.sh A|B|C|D|E nr     set stage, build, stop there
#   ./run.sh build            build only
#   ./run.sh run              open minicom only
#   ./run.sh reset            reset the VM
#   ./run.sh start            start the VM headless
#   ./run.sh stop             power off the VM
#   ./run.sh clean            wipe objects and stale files
#   ./run.sh fail             failures from the last log
#   ./run.sh                  show current stage and usage

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYS="$ROOT/system"
COMPILE="$ROOT/compile"
TESTFILE="$SYS/paging_test.c"
LOG="${XINU_LOG:-$HOME/xinu.log}"
VM="${XINU_VM:-backend}"

die() { echo "run.sh: $*" >&2; exit 1; }

[ -d "$COMPILE" ] || die "no compile/ directory under $ROOT"
[ -f "$TESTFILE" ] || die "cannot find $TESTFILE"

current_stage() {
	grep -o '^#define STAGE_[A-E]' "$TESTFILE" | sed 's/.*STAGE_//' | tr '\n' ' '
}

set_stage() {
	local want="$1" s
	for s in A B C D E; do
		sed -i "s|^#define STAGE_$s\$|/* #define STAGE_$s */|" "$TESTFILE"
	done
	sed -i "s|^/\* #define STAGE_$want \*/\$|#define STAGE_$want|" "$TESTFILE"

	[ "$(current_stage)" = "$want " ] || \
		die "could not enable STAGE_$want -- check the #define lines in paging_test.c"
	echo ">> stage $want"
}

do_clean() {
	echo ">> cleaning"
	rm -f "$COMPILE/binaries/mainorg.o" "$COMPILE/.defs"
	rm -f "$SYS/mainorg.c"
	( cd "$COMPILE" && make clean >/dev/null 2>&1 )
	echo ">> clean"
}

do_build() {
	echo ">> building"
	( cd "$COMPILE" && make 2>&1 | tee "$ROOT/build.log" )
	if ! grep -qi "error" "$ROOT/build.log"; then
		echo ">> build ok"
		return 0
	fi
	echo ""
	echo ">> BUILD FAILED -- first errors:"
	grep -i -m 10 "error" "$ROOT/build.log"
	echo ">> full output in $ROOT/build.log"
	return 1
}

vbox() {
	command -v VBoxManage >/dev/null || die "VBoxManage not on PATH"
	VBoxManage "$@"
}

vm_running() {
	vbox list runningvms 2>/dev/null | grep -q "\"$VM\""
}

do_start() {
	if vm_running; then
		echo ">> $VM already running"
		return 0
	fi
	echo ">> starting $VM headless"
	vbox startvm "$VM" --type headless
}

do_reset() {
	if vm_running; then
		echo ">> resetting $VM"
		vbox controlvm "$VM" reset
	else
		do_start
	fi
	echo ">> watch the output in your minicom shell"
}

do_stop() {
	vm_running || { echo ">> $VM is not running"; return 0; }
	echo ">> powering off $VM"
	vbox controlvm "$VM" poweroff
}

do_run() {
	command -v minicom >/dev/null || die "minicom not on PATH"
	echo ">> starting minicom, logging to $LOG"
	echo ">> Ctrl-A then X to quit"
	sleep 1
	minicom -C "$LOG"
	echo ""
	summarize
}

summarize() {
	[ -f "$LOG" ] || return 0
	echo ">> $(grep -c PASS "$LOG") passed, $(grep -c FAIL "$LOG") failed"
	grep -n FAIL "$LOG" || true
}

show_fail() {
	[ -f "$LOG" ] || die "no $LOG yet -- run a stage first"
	echo ">> failures:"
	grep -n FAIL "$LOG" || echo "   none"
	echo ">> segfaults:"
	grep -n SEGMENTATION_FAULT "$LOG" || echo "   none"
	echo ">> totals: $(grep -c PASS "$LOG") passed, $(grep -c FAIL "$LOG") failed"
}

case "${1:-}" in
	A|B|C|D|E|a|b|c|d|e)
		set_stage "$(echo "$1" | tr 'a-z' 'A-Z')"
		do_build || exit 1
		case "${2:-}" in
			go)	do_reset ;;
			nr)	: ;;
			*)	do_run ;;
		esac
		;;
	build)	do_build ;;
	run)	do_run ;;
	reset)	do_reset ;;
	start)	do_start ;;
	stop)	do_stop ;;
	clean)	do_clean ;;
	fail)	show_fail ;;
	*)
		echo "current stage: $(current_stage)"
		echo "vm:            $VM"
		echo "log:           $LOG"
		echo ""
		echo "usage: ./run.sh {A|B|C|D|E} [go|nr]"
		echo "       ./run.sh {build|run|reset|start|stop|clean|fail}"
		;;
esac