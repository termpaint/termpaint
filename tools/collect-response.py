#! /usr/bin/python3

import os
import termios
import tty
import time
import re
import hashlib

def read_timed():
    time.sleep(1)
    return os.read(0, 100)

def get_pos():
    os.write(1, b'\033[6n')
    r = read_timed()
    m = re.fullmatch(b'\033\\[(?P<row>[0-9]*);(?P<col>[0-9]*)R', r)
    if not m:
        raise RuntimeError('Terminal reply to cursor position report is invalid')
    return int(m['row'].decode()), int(m['col'].decode())

LFLAG = 3
CC = 6

print('working... Do not press any key, this will take about a minute')
print()

orig = termios.tcgetattr(0)

result = []

try:
    mode = termios.tcgetattr(0)
    mode[LFLAG] = mode[LFLAG] & ~(termios.ECHO | termios.ICANON)
    mode[CC][termios.VMIN] = 0
    mode[CC][termios.VTIME] = 0
    termios.tcsetattr(0, termios.TCSAFLUSH, mode)
    pos = get_pos()
    for i in [r'\033[>c', r'\033[>1c', r'\033[>0;1c',r'\033[=c', r'\033[5n', r'\033[6n', r'\033[?6n', r'\033[>q', r'\033[1x', r'\033]4;255;?\007', r'\033P+q544e\033\\']:
        send = i.replace('\\033', '\033').replace('\\007', '\007').replace(r'\\', '\\')
        os.write(1, send.encode())
        ret = read_timed()
        new_pos = get_pos()
        if pos == new_pos:
            result.append((i, ret.replace(b'\\', b'\\\\').replace(b'\033', b'\\033')
                                 .replace(b'\007', b'\\007').replace(b'\x90', b'\\x90')
                                 .replace(b'\x9c', b'\\x9c').decode(), None))
        else:
            if pos[0] == new_pos[0]:
                glitch = 'X' * (new_pos[1] - pos[1])
            else:
                glitch = 'fix this manually'
            print()
            pos = get_pos()
            result.append((i, ret.replace(b'\\', b'\\\\').replace(b'\033', b'\\033')
                                 .replace(b'\007', b'\\007').replace(b'\x90', b'\\x90')
                                 .replace(b'\x9c', b'\\x9c').decode(), glitch))
finally:
    termios.tcsetattr(0, termios.TCSAFLUSH, orig)

fphash = hashlib.new('md5')
for (query, response, glitch) in result:
    if not glitch:
        line = '            { %-19s { "%s" }},' % ('"' + query + '",', response)
    else:
        line = '            { %-19s { "%s", "%s" }},' % ('"' + query + '",', response, glitch)
    print(line)
    fphash.update(line.encode())
print(fphash.hexdigest())


