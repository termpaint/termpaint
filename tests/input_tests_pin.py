#! /usr/bin/python3

"""
Updates input_tests.json from a set of input capture files

This files is usef for test [pin-recorded]
"""

import json

files = [
    'xterm-nobind-compat-105_de_full-2017-10-22:23-fixed.json',
    'xterm-nobind-modother-105_de_full-2017-10-23:21.json',
    'gnome-terminal-compat-105_de_full-2017-10-23:23.json',
    'konsole-compat-105_de_full-2017-10-27:00.json',
    'linuxvt-compat-105_de_full-2017-10-26:21.json',
    'urxvt-nobind-compat-105_de_full-2017-11-22:00.json',
    'xterm-compat+1035_keypad_2019-01-07.json',
    'xterm-nobind--app arrow + app np + esc for alt-105_de_full-2018-05-33:21.json',
    'xterm-nobind-modother-csiu-105_de_full-2019-07-19.json',
]

sequences = {}

sequences['1b5b306e'] = {
    'raw': '1b5b306e',
    'type': 'key',
    'mod': '   ',
    'key': 'i_resync'
}

for filename in files:
    f = open('../tools/miscdata/' + filename, 'r')
    source = json.load(f)
    source_sequences = source['sequences']
    added = False
    for seq in source_sequences:
        if seq['type'] in ('N/A', 'TODO'): continue
        if seq['raw'] not in sequences:
            sequences[seq['raw']] = seq
            added = True
        else:
            existing_seq = sequences[seq['raw']]
            if (existing_seq['type'], existing_seq['mod'], existing_seq.get('key'), existing_seq.get('chars')) != (seq['type'], seq['mod'], seq.get('key'), seq.get('chars')):
                print('{} has different interpretations.'.format(seq['raw']))

    if not added:
        print('file {} did not add anything'.format(filename))

output = []

for key in sorted(sequences.keys()):
    output_sequence = dict(sequences[key])
    try:
        for k in list(output_sequence.keys()):
            if k not in ('raw', 'type', 'mod', 'key', 'chars'):
                del output_sequence[k]

        if output_sequence['type'] == 'key':
            keyId = output_sequence['key']
        else:
            keyId = output_sequence['chars']
        keyId += '.' + output_sequence['mod'].replace(' ', '')
    except:
        print(output_sequence)
        raise

    output_sequence['keyId'] = keyId
    output.append(output_sequence)

outfile = open('input_tests.json', 'w')
json.dump(output, outfile, indent=4, sort_keys=True)
