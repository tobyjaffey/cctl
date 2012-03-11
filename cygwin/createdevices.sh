#!/bin/sh
# Create devices
# Author: Igor Pechtchanski <pechtcha@cs.nyu.edu>
# Version: 1.1
#

# Script parameters
# Number of devices of a certain type to be created - change if more needed
# ttys
CREATE_TTY=32
# serial ttys
CREATE_TTYS=16
# floppy drives
CREATE_FD=3
# compact disks
CREATE_SCD=3
# hard drives
CREATE_SD=3
# hard drive partitions
CREATE_SD_PART=3
# tape drives
CREATE_ST=3
# Parameters end here -- you shouldn't have to change anything below this line

usage() {
  echo "Usage: $0 [--verbose|-v]"
  echo "    or $0 --debug|-n"
  echo "    or $0 --help|-h"
  echo "    or $0 --version|-V"
  echo ""
  echo "--verbose, -v	Print commands as they are executed"
  echo "--debug, -n	Don't execute commands, just print what will be done"
  echo "--help, -h	Print this message and exit"
  echo "--version, -V	Print the program version and exit"
}

if [ "x$1" = "x--debug" -o "x$1" = "x-n" ]; then
  touch() { echo touch "$@"; }
  ln() { echo ln "$@"; }
  mkdir() { echo mkdir "$@"; }
  cd() { echo cd "$@"; }
  shift
elif [ "x$1" = "x--help" -o "x$1" = "x-h" ]; then
  usage
  exit 0
elif [ "x$1" = "x--version" -o "x$1" = "x-V" ]; then
  echo -n "$0, "
  sed -n '/^# Version: /{s/^# // p}' "$0"
  exit 0
elif [ "x$1" = "x--verbose" -o "x$1" = "x-v" ]; then
  set -x
elif [ "x$1" != "x" ]; then
  echo "Invalid parameter: $1"
  usage
  exit 1
fi

error() { echo "$@" && exit 1; }

# Maximum number of tty devices
NUM_TTY=64
[ -z "$CREATE_TTY" ] && CREATE_TTY="$NUM_TTY"
[ "$CREATE_TTY" -gt "$NUM_TTY" ] && error "Too many ttys: $CREATE_TTY"
# Maximum number of ttyS (serial tty) devices
NUM_TTYS=16
[ -z "$CREATE_TTYS" ] && CREATE_TTYS="$NUM_TTYS"
[ "$CREATE_TTYS" -gt "$NUM_TTYS" ] && \
  error "Too many serial ttys: $CREATE_TTYS"
# Maximum number of st (tape) devices
NUM_ST=16
[ -z "$CREATE_ST" ] && CREATE_ST="$NUM_ST"
[ "$CREATE_ST" -gt "$NUM_ST" ] && error "Too many tape devices: $CREATE_ST"
# Maximum number of fd (floppy drive) devices
NUM_FD=16
[ -z "$CREATE_FD" ] && CREATE_FD="$NUM_FD"
[ "$CREATE_FD" -gt "$NUM_FD" ] && error "Too many floppy drives: $CREATE_FD"
# Maximum number of scd (compact disk) devices
NUM_SCD=16
[ -z "$CREATE_SCD" ] && CREATE_SCD="$NUM_SCD"
[ "$CREATE_SCD" -gt "$NUM_SCD" ] && error "Too many compact disks: $CREATE_SCD"
# Maximum number of sd (hard disk) physical devices
NUM_SD=16
[ -z "$CREATE_SD" ] && CREATE_SD="$NUM_SD"
[ "$CREATE_SD" -gt "$NUM_SD" ] && error "Too many hard drives: $CREATE_SD"
# Maximum number of sd (hard disk) partition devices
NUM_SD_PART=16
[ -z "$CREATE_SD_PART" ] && CREATE_SD_PART="$NUM_SD_PART"
[ "$CREATE_SD_PART" -gt "$NUM_SD_PART" ] && \
  error "Too many hard drive partitions: $CREATE_SD_PART"
# Sequence of numbers from $2(0) to $1-1
NUMS() {
  [ -z "$2" ] && set -- "$1" 0;
  seq -s " " $2 1 $(expr $1 - 1) 2>/dev/null;
}
# Sequence of letters from 'a' to 'a'+$1-1
LTRS() {
  /bin/echo -e $(printf '\\%o ' $(seq -s " " 97 1 $(expr 96 + $1) 2>/dev/null));
}

# Actual /dev directory
DEVDIR="$(cygpath -au "C:/$(cygpath -aw /dev)" | sed 's,/c/\(.\):/,/\1/,')"
[ -e "$DEVDIR" -a ! -d "$DEVDIR" ] && \
   error "$DEVDIR exists and is not a directory"
[ ! -e "$DEVDIR" ] && \
   (mkdir "$DEVDIR" || error "Unable to create $DEVDIR")
[ ! -e "$DEVDIR" -o -w "$DEVDIR" ] || \
   error "$DEVDIR exists, but isn't writeable"
cd "$DEVDIR" || error "Unable to cd to $DEVDIR"

# - (0,252): fifo
for i in fifo; do touch ./"$i"; done
# - (1,*): mem kmem null zero port random urandom
for i in mem kmem null zero port random urandom; do touch ./"$i"; done
# - (2,*): floppies
for i in $(NUMS $CREATE_FD); do touch ./"fd$i"; done
# - (5,*): tty console ptmx conout conin
for i in tty console ptmx conout conin; do touch ./"$i"; done
# - (8,*): hard disks
for j in $(LTRS $CREATE_SD); do
  for i in "" $(NUMS $CREATE_SD_PART 1); do touch ./"sd$j$i"; done
done
# - (9,*) tape drives (rewind/no rewind)
for i in $(NUMS $CREATE_ST); do touch ./"st$i"; done
for i in $(NUMS $CREATE_ST); do touch ./"nst$i"; done
# - (11,*) compact disks
for i in $(NUMS $CREATE_SCD); do touch ./"scd$i"; done
for i in $(NUMS $CREATE_SCD); do touch ./"sr$i"; done
# - (13,*) clipboard windows
for i in clipboard windows; do touch ./"$i"; done
# - (14,3) dsp
for i in dsp; do touch ./"$i"; done
# - (117,*) serial
for i in $(NUMS $CREATE_TTYS); do touch ./"ttyS$i"; done
for i in com0; do touch ./"$i"; done
#   TODO: how do we deal with the other com*?
# - (128,0) tty master
for i in ttym; do touch ./"$i"; done
# - (136,*) tty
for i in $(NUMS $CREATE_TTY); do touch ./"tty$i"; done

# Create pipes
for i in pipe; do touch ./"$i"; done

# Create symbolic links
[ ! -e console ] && ln -s tty ./console
[ ! -e floppy ]  && ln -s fd0 ./floppy
[ ! -e cdrom ]   && ln -s scd0 ./cdrom
[ ! -e tape ]    && ln -s st0 ./tape
[ ! -e audio ]   && ln -s dsp ./audio

