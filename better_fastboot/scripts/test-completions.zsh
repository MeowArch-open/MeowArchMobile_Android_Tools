#!/usr/bin/env zsh
# test-completions.zsh — functional test of the generated zsh completion.
#
# Drives the completion function directly with a capturing compadd stub (no
# ZLE context needed) and asserts the right vocabulary comes back for each
# word position. Usage:
#
#   zsh scripts/test-completions.zsh <path-to-generated-_mfastboot>
#
# A real zsh is required (not installed by default everywhere); test-ux.sh
# skips this when `zsh` is missing.
emulate -L zsh
setopt no_unset_err 2>/dev/null

local script=${1:?usage: test-completions.zsh <completion-script>}
[[ -f $script ]] || { print "no such file: $script"; exit 2 }

local dir=${script:h}
local fn=${script:t}
# the file must be named _mfastboot for autoload; copy if needed
local tmpdir=$(mktemp -d)
cp $script $tmpdir/_mfastboot
fpath=($tmpdir)
autoload -Uz _mfastboot

typeset -g CAPTURED=""
typeset -i pass=0 fail=0

# compadd stub: record the candidate words (expanding `-a arrayname`)
compadd() {
  local -a items
  local a an
  while (( $# > 0 )); do
    a=$1; shift
    if [[ $a == -a ]]; then
      an=$1; shift
      items+=("${(@P)an}")
    elif [[ $a == -[PSMdJVxXshDOACFWpqrR1nQf] ]]; then
      # flag taking an argument (approximation of compadd's grammar)
      if [[ $a == -Q || $a == -f || $a == -n ]]; then :
      else shift; fi
    elif [[ $a == -* ]]; then
      continue
    else
      items+=($a)
    fi
  done
  CAPTURED="${CAPTURED} ${(j: :)items} "
}
_files() { CAPTURED="${CAPTURED} <files>" }

try() {
  local desc=$1 want=$2; shift 2
  CAPTURED=""
  words=(mfastboot "$@")
  CURRENT=${#words}
  _mfastboot >/dev/null 2>&1
  if [[ $CAPTURED == *"$want"* ]]; then
    print "  ok   $desc"; pass+=1
  else
    print "  FAIL $desc: want '$want', got '${CAPTURED:0:90}'"; fail+=1
  fi
}

try "empty -> commands"        "flash"           ""
try "flas prefix -> flash"     "flash"           "flas"
try "flash -> partitions"      "vendor_boot"     "flash" ""
try "getvar -> variables"      "current-slot"    "getvar" ""
try "--slot -> slots"          "other"           "--slot" ""
try "--lang -> langs"          "zh_TW"           "--lang" ""
try "reboot -> targets"        "bootloader"      "reboot" ""
try "flashing -> subs"         "unlock_critical" "flashing" ""
try "gsi -> subs"              "status"          "gsi" ""
try "snapshot-update -> subs"  "merge"           "snapshot-update" ""
try "set_active -> slots"      "all"             "set_active" ""
try "- -> options"             "--no-progress"   "-"
try "-v then command"          "flash"           "-v" "fl"
try "--lang= form"             "ja"              "--lang="
try "fetch -> partitions"      "dtbo"            "fetch" ""
try "erase -> partitions"      "userdata"        "erase" ""
try "format -> partitions"     "vbmeta"          "format" ""
# flash PARTITION [FILENAME]: argument 2 is a file, argument 1 a partition
try "flash2 -> img files"      "<files>"         "flash" "boot_a" ""
try "flash:raw2 -> img files"  "<files>"         "flash:raw" "boot" ""
run_flash2_negative() {
  CAPTURED=""
  words=(mfastboot flash boot_a "")
  CURRENT=${#words}
  _mfastboot >/dev/null 2>&1
  if [[ $CAPTURED == *"vendor_boot"* ]]; then
    print "  FAIL flash2 offers partitions: '$CAPTURED'"; fail+=1
  else
    print "  ok   flash2 does not offer partitions"; pass+=1
  fi
}
run_flash2_negative

# flash-x PARTITION PAYLOAD [NAME]: argument 1 is a partition, argument 2 the
# payload file, argument 3 an entry name inside the payload.
try "flash-x -> partitions"    "vendor_boot"     "flash-x" ""
try "flash-x2 -> files"        "<files>"         "flash-x" "boot" ""
try "flash-x3 -> entries"      "vendor_boot"     "flash-x" "boot" "payload.bin" ""
run_flashx_negative() {
  CAPTURED=""
  words=(mfastboot flash-x boot "")
  CURRENT=${#words}
  _mfastboot >/dev/null 2>&1
  if [[ $CAPTURED == *"vendor_boot"* ]]; then
    print "  FAIL flash-x2 offers partitions: '$CAPTURED'"; fail+=1
  else
    print "  ok   flash-x2 does not offer partitions"; pass+=1
  fi
}
run_flashx_negative

print "== zsh completion: $pass passed, $fail failed =="
rm -rf $tmpdir
[[ $fail -eq 0 ]]
