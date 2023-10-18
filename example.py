#!/usr/bin/python3

# Example script for displaying local airport weather METARs on the LED board.
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

import subprocess
from datetime import datetime, timezone
import http.client

AIRPORTS = [
	"PAO",
	"NUQ",
	"HWD",
	"LVK",
	"C83",
	"TCY",
	"CCR",
	"OAK",
	"DVO",
	"O69",
	"APC",
	"STS",
	"HAF",
	"SFO",
	"RHV",
	"SJC",
	"E16",
	"CVH",
	"WVI",
	"OAR",
	"MRY",
]

# The HTTP API requires you to provide a "KILO" prefix, even for airports that
# don't actually have one (like C83)...
def airports():
	return ",".join("K" + aid for aid in AIRPORTS)

def get_metars():
	try:
		c = http.client.HTTPSConnection("aviationweather.gov")
		c.request("GET", f"/cgi-bin/data/metar.php?ids={airports()}&hours=0&format=raw", "", {})
		metars = c.getresponse().read().decode("ascii", "ignore").splitlines()
	except:
		metars = []
	finally:
		c.close()

	metarsout = []
	metarsout.append(" " * 16 + "LOCAL METARS...")
	now = datetime.now(timezone.utc)
	for metar in metars:
		metarsplit = metar.split(' ')

		# Delete remarks
		if "RMK" in metarsplit:
			del metarsplit[metarsplit.index("RMK"):]

		# Show the airport ID again at the end
		metarsplit.append(metarsplit[0])

		# Decode METAR timestamp (DDHHMM)
		# FIXME: Wrong crossing midnight last day of month
		mtime = datetime(
			year=now.year,
			month=now.month,
			day=int(metarsplit[1][0:2]),
			hour=int(metarsplit[1][2:4]),
			minute=int(metarsplit[1][4:6]),
			tzinfo=timezone.utc,
		)
		delta = now - mtime

		# Convert the UTC timestamp to the local timezone
		#metarsplit[1] = mtime.astimezone().strftime("%H%M%Z")

		# Convert the UTC timestamp to a relative one
		if delta.seconds >= 3600:
			metarsplit[1] = "%.1f HOURS AGO" % (delta.seconds / 3600)
		else:
			metarsplit[1] = "%02d MINUTES AGO" % (delta.seconds // 60)

		# Convert C to F and append it
		for f in filter(lambda x: "/" in x, (x for x in metarsplit)):
			try:
				t, d = f.split("/")
				t = t.replace("M", "-")
				d = d.replace("M", "-")
				temp = int(t)
				dew = int(d)
				faren = "%s%02dF/%s%02dF" % (
					"M" if temp < 0 else "", abs(temp * 1.8 + 32),
					"M" if dew < 0 else "", abs(dew * 1.8 + 32),
				)
				metarsplit[metarsplit.index(f)] += " " + faren
			except:
				continue

		metarsout.append(" ".join(metarsplit))

	if len(metarsout) == 1:
		return ["HELP I AM BROKEN"]

	return [(" " * 8).join(metarsout) + " " * 16]

def run_client(*args):
	cmd = ["./driver.py"]
	cmd.extend(args)
	subprocess.check_call(cmd)

while True:
	run_client(*(["--col-shift", "0.0375"] + get_metars()))
