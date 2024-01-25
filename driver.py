#!/usr/bin/python3

# Python UART client for 6x64 LED display board
#
# Written by Calvin Owens <jcalvinowens@gmail.com>
#
# To the extent possible under law, I waive all copyright and related or
# neighboring rights. You should have received a copy of the CC0 license along
# with this work. If not, see http://creativecommons.org/publicdomain/zero/1.0
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import argparse
import random
import serial
import sys
import time

from ledfont import FONTTABLE

NR_COLS = 64
NR_ROWS = 6

def encode_output_frame(frame):
	# Group every eight bools into a sub-list for conversion.
	bframe = [[
		list(frame[i][j:j + 8]) for j in range(0, NR_COLS, 8)
	] for i in range(NR_ROWS)]

	# Convert each sublist of eight bools into an 8-bit integer
	enc = bytes()
	for row in bframe:
		for byte in row:
			v = sum((byte[i] << i) for i in range(8))
			enc += v.to_bytes(1, "big")

	return enc

def build_output_frame(outstring):
	frame = [[] for _ in range(NR_ROWS)]

	# We can simplify the logic in encode_output_frame() by forcing input
	# strings to be even lengths (and thus a multiple of 8 bits long).
	outstring += " " * (16 - len(outstring))
	if (len(outstring) & 1):
		outstring += " "

	for c in outstring:
		for i, rowbits in enumerate(FONTTABLE[ord(c) - ord(' ')]):
			frame[i].extend(bool(x) for x in rowbits)

	return frame

def build_output_framelist(*msgs):
	return [build_output_frame(outstring) for outstring in msgs]

def make_rowshift_frames(last, now):
	return [
		[last[1], last[2], last[3], last[4], last[5], now[0]],
		[last[2], last[3], last[4], last[5], now[0], now[1]],
		[last[3], last[4], last[5], now[0], now[1], now[2]],
		[last[4], last[5], now[0], now[1], now[2], now[3]],
		[last[5], now[0], now[1], now[2], now[3], now[4]],
		[now[0], now[1], now[2], now[3], now[4], now[5]],
	]

def write_output(where, frame):
	if where is None:
		sys.stdout.write("\x1b[2J\x1b[H")
		sys.stdout.flush()
		for row in frame:
			print("[", "".join(["*" if x else " " for x in row]), "]")

		time.sleep(1 / 38400 * 50)
		return

	where.write(encode_output_frame(frame))

def write_output_frame(where, frame, last, rowslp, initslp, colslp):
	now = []

	for i in range(max(len(frame[0]) - NR_COLS, 1)):
		now = [row[i : i + 64] for row in frame]

		if i == 0 and last is not None:
			for sf in make_rowshift_frames(last, now):
				write_output(where, sf)
				time.sleep(rowslp)

			if len(frame[0]) > NR_COLS:
				time.sleep(initslp)

			continue

		write_output(where, now)
		time.sleep(colslp)

	return now

def write_test_pattern_forever(where):
	while True:
		for i in range(NR_ROWS):
			frame = [[False] * NR_COLS] * NR_ROWS
			frame[i] = [True] * NR_COLS
			write_output(where, frame)
			time.sleep(0.1)

		for i in range(NR_COLS):
			frame = [[False] * NR_COLS] * NR_ROWS
			for j in range(NR_ROWS):
				frame[j][i] = True

			write_output(where, frame)
			time.sleep(0.05)

def parse_arguments():
	parser = argparse.ArgumentParser()
	parser.add_argument("--all-on", action="store_true",
			    help="Turn all 384 LEDs ON")
	parser.add_argument("--port", type=str, default="/dev/ttyUSB0",
			    help="Serial port (ex /dev/ttyS0)")
	parser.add_argument("--simulate", action="store_true",
			    help="Use ASCII-art to simulate board for testing")
	parser.add_argument("--truncate", action="store_true",
			    help="Don't scroll horizontally, truncate")
	parser.add_argument("--row-shift-in", action="store_true",
			    help="Shift in from the top at start")
	parser.add_argument("--row-shift-out", action="store_true",
			    help="Shift out through the bottom at end")
	parser.add_argument("--row-out-hold", type=float, default=1, metavar="SECS",
			    help="How long to wait before rowshifting out")
	parser.add_argument("--row-in-hold", type=float, default=1, metavar="SECS",
			    help="How long to wait before rowshifting in")
	parser.add_argument("--hold-time", type=float, default=0, metavar="SECS",
			    help="How long to leave each string up")
	parser.add_argument("--row-shift", type=float, default=0.05, metavar="SECS",
			    help="Sleep time between each individual row shift")
	parser.add_argument("--col-shift", type=float, default=0.01, metavar="SECS",
			    help="Sleep time betwen each column shift")
	parser.add_argument("--test-pattern", action="store_true",
			    help="Run test patterns")
	parser.add_argument("strings", type=str, nargs="*",
			    help="Strings to write to LEDs (stdin if none)")
	return parser.parse_args()

def main():
	args = parse_arguments()
	where = None if args.simulate else serial.Serial(args.port, 38400)

	if args.all_on:
		write_output_frame(where, [[True] * NR_COLS] * NR_ROWS,
				   None, 0, 0, 0)
		return 0

	if args.test_pattern:
		write_test_pattern_forever(where)
		return 0

	out = build_output_framelist(*args.strings)
	last = None

	if args.truncate:
		for i, frame in enumerate(out):
			for j, row in enumerate(frame):
				out[i][j] = row[0:NR_COLS]

	if args.row_shift_in:
		last = write_output_frame(where, [[False] * NR_COLS] * NR_ROWS,
					  last, args.row_shift, args.row_in_hold,
					  args.col_shift)
	if len(out) == 0:
		while True:
			nxt = sys.stdin.readline()
			if not nxt:
				break

			last = write_output_frame(where,
						  build_output_frame(nxt.rstrip()),
						  last, args.row_shift,
						  args.row_in_hold,
						  args.col_shift)
			time.sleep(args.hold_time)
	else:
		for frame in out:
			last = write_output_frame(where, frame, last,
						  args.row_shift,
						  args.row_in_hold,
						  args.col_shift)
			time.sleep(args.hold_time)

	if args.row_shift_out:
		write_output_frame(where, [[False] * NR_COLS] * NR_ROWS, last,
				   args.row_shift, 0, args.col_shift)
		time.sleep(args.row_out_hold)

	return 0

if __name__ == "__main__":
	sys.exit(main())
