# One command per board, run from the repo root:
#
#   make board-a                       # flash Board A on /dev/ttyUSB0
#   make board-a PORT_A=/dev/ttyUSB1   # ...or a different port
#   make board-b                       # flash Board B on /dev/ttyUSB0
#   make board-b PORT_B=/dev/ttyUSB1
#
# Each target just calls that board's own flash.sh -- see
# board-a-controller/flash.sh and board-b-wii/flash.sh for what actually
# runs, and docs/HARDWARE_SETUP.md for one-time toolchain setup.
#
# There's no single command that does both boards, on purpose: they use
# two unrelated toolchains (arduino-cli vs ESP-IDF) and you should flash
# and check each one independently before wiring them together anyway --
# see docs/STATUS.md's bring-up order.

PORT_A ?= /dev/ttyUSB0
PORT_B ?= /dev/ttyUSB0

.PHONY: board-a board-b

board-a:
	./board-a-controller/flash.sh $(PORT_A)

board-b:
	./board-b-wii/flash.sh $(PORT_B)
