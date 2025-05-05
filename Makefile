# Makefile to compile and run signal_demo_q1, q2, q3

CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGETS = signal_demo_q1 signal_demo_q2 signal_demo_q3

all: $(TARGETS)

signal_demo_q1: signal_demo_q1.c
	$(CC) $(CFLAGS) -o $@ $<

signal_demo_q2: signal_demo_q2.c
	$(CC) $(CFLAGS) -o $@ $<

signal_demo_q3: signal_demo_q3.c
	$(CC) $(CFLAGS) -o $@ $<

run_q1: signal_demo_q1
	./signal_demo_q1

run_q2: signal_demo_q2
	./signal_demo_q2

run_q3: signal_demo_q3
	./signal_demo_q3

clean:
	rm -f $(TARGETS)
