#!/usr/bin/env bash
# test_signals.sh  —  run Q1, Q2, Q3 automatically.
# signal_demo_q1 & signal_demo_q3 must be in the PATH or cwd.

set -euo pipefail
IFS=$'\n\t'

#─── helper arrays ────────────────────────────────────────────────────────────
signals=(INT ABRT ILL CHLD SEGV FPE HUP TSTP)  # The 8 signals
logdir=logs
mkdir -p "$logdir"

#─── Q1: external signals to main ─────────────────────────────────────────────
run_q1(){
  echo
  echo "===== Q1: main process receives each signal once, then twice ====="
  logfile="$logdir/q1.log"
  rm -f "$logfile"

  echo "-- starting signal_demo_q1 --"
  ./signal_demo_q1 2>&1 | tee "$logfile" &
  pid=$!
  sleep 2   # let it initialize

  echo "-- sending each signal ONCE to PID $pid --"
  for s in "${signals[@]}"; do
    echo "  kill -SIG${s} $pid"
    kill -SIG${s} "$pid"
    sleep 1
  done

  echo "-- sending each signal TWICE to PID $pid --"
  for s in "${signals[@]}"; do
    echo "  kill -SIG${s} $pid  (1st)"
    kill -SIG${s} "$pid"
    sleep 0.5
    echo "  kill -SIG${s} $pid  (2nd)"
    kill -SIG${s} "$pid"
    sleep 1
  done

  echo "-- terminating Q1 run --"
  kill -SIGTERM "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true

  echo "Q1 log saved to $logfile"
}

#─── Q2: signals sent from “child” → child vs parent ───────────────────────────
run_q2(){
  echo
  echo "===== Q2: send each signal once & twice to one child at a time, then to parent ====="
  logfile="$logdir/q2.log"
  rm -f "$logfile"

  echo "-- starting signal_demo_q1 (re-using Q1 binary for Q2) --"
  ./signal_demo_q1 2>&1 | tee "$logfile" &
  pid=$!
  sleep 2   # allow children to fork & print PIDs

  # extract the first child’s PID (for “one child sends to another” scenario you’d adapt)
  child_pids=( $(grep "forked child" "$logfile" \
                  | head -n4 \
                  | awk '{print $5}') )
  target="${child_pids[0]}"
  echo "-- using child PID $target as our “sender/receiver” target --"

  echo "-- sending each signal ONCE to child $target --"
  for s in "${signals[@]}"; do
    echo "  kill -SIG${s} $target"
    kill -SIG${s} "$target"
    sleep 1
  done

  echo "-- sending each signal TWICE to child $target --"
  for s in "${signals[@]}"; do
    kill -SIG${s} "$target"
    sleep 0.5
    kill -SIG${s} "$target"
    sleep 1
  done

  echo "-- now sending each signal ONCE to parent PID $pid --"
  for s in "${signals[@]}"; do
    kill -SIG${s} "$pid"
    sleep 1
  done

  echo "-- now sending each signal TWICE to parent PID $pid --"
  for s in "${signals[@]}"; do
    kill -SIG${s} "$pid"
    sleep 0.5
    kill -SIG${s} "$pid"
    sleep 1
  done

  echo "-- terminating Q2 run --"
  kill -SIGTERM "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true

  echo "Q2 log saved to $logfile"
}

#─── Q3: proper masking + pending‐queue inspection ─────────────────────────────
run_q3(){
  echo
  echo "===== Q3: block sets, send each signal 3× to parent (pre-fork) & to each child ====="
  logfile="$logdir/q3.log"
  rm -f "$logfile"

  echo "-- starting signal_demo_q3 --"
  ./signal_demo_q3 2>&1 | tee "$logfile" &
  pid=$!

  # wait for the “READY_FOR_SIGNALS” marker from the parent
  echo "-- waiting for READY_FOR_SIGNALS marker in log --"
  until grep -q "^READY_FOR_SIGNALS" "$logfile"; do
    sleep 0.1
  done

  echo "-- sending each signal *3 times* to PARENT pid=$pid (pre-fork window) --"
  for s in "${signals[@]}"; do
    for i in 1 2 3; do
      kill -SIG${s} "$pid"
      sleep 0.2
    done
  done

  # now wait until the children are forked and printed
  echo "-- waiting for CHILD_PIDS line in log --"
  until grep -q "^CHILD_PIDS:" "$logfile"; do
    sleep 0.1
  done

  child_pids=( $(grep "^CHILD_PIDS:" "$logfile" \
                  | awk '{for(i=2;i<=NF;i++) printf("%s ",$i);}') )

  echo "-- sending each signal *3 times* to each CHILD pid --"
  for c in "${child_pids[@]}"; do
    for s in "${signals[@]}"; do
      for i in 1 2 3; do
        kill -SIG${s} "$c"
        sleep 0.2
      done
    done
  done

  # give them a moment to collect into their pending queues and print
  echo "-- giving processes a moment to inspect & print pending sets --"
  sleep 2

  echo "-- terminating Q3 run --"
  kill -SIGTERM "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true

  echo "Q3 log saved to $logfile"
}

#─── main ─────────────────────────────────────────────────────────────────────
echo ">>> Running all tests.  Logs in $logdir/"
run_q1
run_q2
run_q3
echo ">>> All done.  Inspect $logdir/*.log for full output."
