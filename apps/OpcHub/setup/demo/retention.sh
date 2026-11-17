#!/usr/bin/env bash
# The public demo's retention (docs/demo/plan.md Phase 10;  README.md beside this file is the runbook):  the historian never
# deletes, so jde-demo-retention.timer runs this daily, as jde-cpp.  It removes each day's directory under the historian's
# path, <yyyy>/<m>/<d> with every group's files of that day, older than --days:  a read over a removed day sees no record
# (libs/historian/docs/spec.md, Retention).  A month or a year it empties goes with its last day.  The OEE rows are the
# database's, and kept.
#   retention.sh [--path <dir>] [--days <n>] [--dry-run]
#     --path      the gateway's hist.path                          default: /var/lib/Jde-Cpp/OpcHub/hist/opc-gateway
#     --days      the days kept before today's, in UTC             default: 90
#     --dry-run   says what it would remove, and removes nothing
# Left, and said:  a day holding a temp file, which is a rewrite of that day at work, or one a crash cut short that the
# historian removes at its next start.  Anything not named as the historian names a day is not looked at.
set -euo pipefail
die(){ echo "retention.sh: $*" >&2; exit 1; }
path=/var/lib/Jde-Cpp/OpcHub/hist/opc-gateway; days=90; dry=0
while [ $# -gt 0 ]; do
	case "$1" in
		--path) path=${2:?}; shift 2;;   --path=*) path=${1#*=}; shift;;
		--days) days=${2:?}; shift 2;;   --days=*) days=${1#*=}; shift;;
		--dry-run) dry=1; shift;;
		-h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
		*) die "unknown option '$1'";;
	esac
done
[[ $days =~ ^[1-9][0-9]*$ ]] || die "--days is a number of days, 1 or more:  today's file is the one being written"
path=${path%/}
[ -d "$path" ] || { echo "retention.sh: no $path:  nothing is stored yet"; exit 0; }

first=$(date -u -d "$days days ago" +%Y%m%d) #the first day kept
removed=0; left=0
while IFS= read -r -d '' dir; do
	#as the historian names a day (libs/fwk/src/io/records/Days.cpp):  numbers with no padding
	[[ ${dir#"$path"/} =~ ^([1-9][0-9]{3})/([1-9]|1[0-2])/([1-9]|[12][0-9]|3[01])$ ]] || continue
	day=$(printf '%d%02d%02d' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}")
	[ "$day" -lt "$first" ] || continue
	if [ -n "$(find "$dir" -name '*.tmp' -print -quit)" ]; then
		echo "retention.sh: left ${dir#"$path"/}, which holds a temp file"
		left=$((left+1))
	elif [ $dry = 1 ]; then
		echo "retention.sh: would remove ${dir#"$path"/}"
		removed=$((removed+1))
	else
		rm -rf -- "$dir"
		removed=$((removed+1))
	fi
done < <(find "$path" -mindepth 3 -maxdepth 3 -type d -print0)
[ $dry = 1 ] || find "$path" -mindepth 1 -maxdepth 2 -type d -regextype posix-extended -regex "$path/[0-9]+(/[0-9]+)?" -empty -delete
echo "retention.sh: $([ $dry = 1 ] && echo "would remove" || echo removed) $removed day directories before $(date -u -d "$days days ago" +%F) under $path, left $left"