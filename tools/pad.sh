#!/bin/bash
# usage: pad.sh <buttons-hex|name> [ms] [wait_s] [shotname] [lx ly rx ry lt rt]
SC="${RIDDICKFIX_TESTDIR:-$(cygpath -m "$TEMP")/RiddickFixTest}"; mkdir -p "$SC"
F="$SC/pad_live.txt"
case "$1" in
  A) B=1000;; B) B=2000;; X) B=4000;; Y) B=8000;; START) B=10;; BACK) B=20;; UP) B=1;; DOWN) B=2;; LEFT) B=4;; RIGHT) B=8;;
  LB) B=100;; RB) B=200;; LS) B=40;; RS) B=80;; NONE) B=0;; *) B=$1;;
esac
MS=${2:-150}; W=${3:-1.5}
SEQ=$(( $(cut -d' ' -f1 "$F" 2>/dev/null || echo 0) + 1 ))
echo "$SEQ $B $MS ${5:-0} ${6:-0} ${7:-0} ${8:-0} ${9:-0} ${10:-0}" > "$F"
sleep "$W"
if [ -n "$4" ]; then
  /c/Tools/bin/ffmpeg.exe -loglevel error -filter_complex "ddagrab=output_idx=0,hwdownload,format=bgra,scale=1920:-1" -frames:v 1 -update 1 -y "$SC/$4.png"
fi
