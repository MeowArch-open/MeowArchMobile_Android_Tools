#!/usr/bin/env zsh
# test-completions.zsh — functional test of the generated zsh completion.
#
# Drives the completion function directly with a capturing compadd stub (no ZLE
# context needed) and asserts the right vocabulary comes back at each word
# position. Usage:
#
#   zsh scripts/test-completions.zsh <generated-_adb-m> [serial]
#
# With a serial, the device-side checks (remote paths, the device's own
# executables, package names) run for real against that device; without one they
# are skipped, because a completion that returns nothing is indistinguishable
# from a completion that found no device. A real zsh is required — test-adb.sh
# skips this when zsh is missing.
emulate -L zsh
setopt no_unset_err 2>/dev/null

local script=${1:?usage: test-completions.zsh <completion-script> [serial] [adb-binary]}
[[ -f $script ]] || { print "no such file: $script"; exit 2 }
local serial=${2:-}
# The completion's device-side helpers run `${words[1]}`, i.e. `adb-m`, which the
# caller must have put on PATH (test-adb.sh symlinks it into a scratch bin).
# This binary is only for the harness's own probing below.
local adb_bin=${3:-adb-m}

# The script must be named _adb-m for autoload, and it registers both command
# names via its #compdef line.
local tmpdir=$(mktemp -d)
cp $script $tmpdir/_adb-m
fpath=($tmpdir)
autoload -Uz _adb-m

typeset -g CAPTURED=""
typeset -i pass=0 fail=0 skip=0

# compadd stub: record the candidate words, expanding `-a arrayname`.
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
  words=(adb-m "$@")
  CURRENT=${#words}
  # Call the AUTOLOADED name (dash, not underscore): zsh loads the file on
  # first call, and the file's own trailing `_adb_m "$@"` then runs it. Calling
  # _adb_m directly would hit an undefined function, since autoload is lazy.
  _adb-m >/dev/null 2>&1
  if [[ $CAPTURED == *"$want"* ]]; then
    print "  ok   $desc"; pass+=1
  else
    print "  FAIL $desc: want '$want', got '${CAPTURED:0:110}'"; fail+=1
  fi
}

# Host-side vocabulary: no device involved.
try "empty -> commands"        "push"           ""
try "prefix pu -> push"        "push"           "pu"
try "options"                  "--no-progress"  "-"
try "--lang -> langs"          "zh_TW"          "--lang" ""
try "--lang= form"             "ja"             "--lang="
try "reboot -> targets"        "bootloader"     "reboot" ""
try "sync -> targets"          "system"         "sync" ""
try "mdns -> subs"             "services"       "mdns" ""
try "forward -> subs"          "remove-all"     "forward" ""
try "help -> sections"         "networking"     "help" ""
try "install -> apk files"     "<files>"        "install" ""
try "sideload -> zip files"    "<files>"        "sideload" ""
try "push LOCAL -> files"      "<files>"        "push" ""
try "pull LOCAL -> files"      "<files>"        "pull" "/x" ""
# An option's value must not be mistaken for the command: `-s SERIAL pu<TAB>`
# still completes `push`.
try "-s value skipped"         "push"           "-s" "abc123" "pu"

if [[ -z $serial ]]; then
  print "  skip device-side checks (no serial given)"
  skip=3
else
  export ANDROID_SERIAL=$serial
  # Device-side: these query the device, so they are only meaningful with one
  # attached. Each is read-only (ls / pm list / echo $PATH).
  try "pull REMOTE -> paths"   "/"              "pull" "/"
  try "shell -> device cmds"   "ls"             "shell" ""
  try "pm list -> kinds"       "packages"       "shell" "pm" "list" ""
  # `pm` is part of Android's framework, not of adbd: a glibc adbd has no
  # package manager, so an empty package list there is correct and asserting on
  # it would fail for a reason that has nothing to do with the completion.
  if $adb_bin -s $serial shell 'command -v pm' >/dev/null 2>&1; then
    try "uninstall -> packages"  "."            "uninstall" ""
  else
    print "  skip uninstall -> packages (device has no pm)"
    skip+=1
  fi
fi

print "== zsh completion: $pass passed, $fail failed, $skip skipped =="
rm -rf $tmpdir
[[ $fail -eq 0 ]]
