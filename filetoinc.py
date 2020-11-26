#! /usr/bin/env python3
# SPDX-License-Identifier: BSL-1.0

import sys

name = sys.argv[1]
infile = sys.argv[2]
outfile = sys.argv[3]

with open(infile, 'rb') as f:
    data = f.read()

data_encoded = [""]

# intentionally excluding some whitespace and some special characters
basic_c_source_chars = set(b' abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_{}[]#()<>%:;.?*+-/^&|~!=,\'')

for ch in data:
    if ch in basic_c_source_chars:
        data_encoded[-1] += chr(ch)
    else:
        data_encoded[-1] += "\\{:o}".format(ch)

    if len(data_encoded[-1]) > 70:
        data_encoded.append("")

with open(outfile, 'w') as f:
    f.write('static const unsigned char {}[] = {{\n'.format(name))
    for line in data_encoded:
        f.write('    "{}"\n'.format(line))
    f.write('};\n')
