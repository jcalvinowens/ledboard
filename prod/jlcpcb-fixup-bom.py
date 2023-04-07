#!/usr/bin/python3

## JLCPCB can't handle more than 128 items per line in BOM files. This script
## will split any such lines into as many 128-item lines as is needed. The BOM
## files in this directory have already been fixed with this script.

import copy
import sys

def chunks(l, n):
	for i in range(0, len(l), n):
		yield l[i : i + n]

if len(sys.argv) != 2:
	print("Usage: ./jlcpcb-fixup-bom.py ./BOM.csv")
	sys.exit(0)

with open(sys.argv[1], "rb") as f:
	INPUT_BOM = f.read().decode('utf-16').splitlines()

INDATA = []
HEADERS = INPUT_BOM[0].split('\t')
for line in INPUT_BOM[1:]:
	row = line.split('\t')
	this = {}

	for i, field in enumerate(HEADERS):
		this[field] = row[i][1:-1]

	INDATA.append(this)

IDX = 1
OUTDATA = []
for i, item in enumerate(INDATA):
	if int(item["Quantity"]) <= 128:
		item["ID"] = str(IDX)
		IDX += 1

		OUTDATA.append(item.values())
		continue

	splitlist = item["Designator"].split(',')
	for chunk in chunks(splitlist, 128):
		new = copy.deepcopy(item)
		new["Designator"] = ','.join(chunk)
		new["Quantity"] = str(len(chunk))
		new["ID"] = str(IDX)
		IDX += 1

		OUTDATA.append(new.values())

FINAL = '\t'.join(HEADERS) + '\n'
for line in OUTDATA:
	quoted = ['"' + s + '"' for s in line]
	FINAL += '\t'.join(quoted) + '\n'

with open(sys.argv[1], "wb+") as f:
	f.write(FINAL.encode('utf-16'))

if IDX - 1 != len(INDATA):
	print(f"Success! Split {IDX - len(INDATA) - 1} new lines")
else:
	print("Success! No lines needed to be split")
