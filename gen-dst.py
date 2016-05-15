#! /usr/bin/env python3

import subprocess
import re
import time
import calendar
import sys
import contextlib

@contextlib.contextmanager
def open_output():
    if len(sys.argv) < 2:
        yield sys.stdout
    else:
        with open(sys.argv[1], "wt") as f:
            yield f


def main():
    with open("/etc/timezone") as f:
        tz = f.readline().strip()
    out = subprocess.check_output(["zdump", "-v", "-c", "2015,2038", tz])
    prev_offset = 0
    with open_output() as fout:
        fout.write("typedef struct {time_t start; int offset;} tzentry;\n")
        fout.write("static const tzentry tzdata[] = {\n")
        for line in out.decode().splitlines():
            if line.endswith("NULL"):
                continue
            if not line.startswith(tz):
                continue
            line = line[len(tz):]
            m = re.match(" *(.*) UT = .* gmtoff=([0-9]+)", line)
            if m is None:
                continue
            offset = int(m.group(2))
            if offset == prev_offset:
                continue
            prev_offset = offset

            t = time.strptime(m.group(1))
            secs = calendar.timegm(t)
            fout.write(" 0x%x, %d,\n" % (secs, offset))
        fout.write(" 0, 0\n")
        fout.write("};\n")

if __name__ == "__main__":
    main()
