#! /usr/bin/python3
# SPDX-License-Identifier: BSL-1.0

import elftools
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import NoteSection, SymbolTableSection

# C standard functions and widespread kernel independent posix
# (_chk variants are what some libc implementations use internally)
# Allowed in portable core
import_whitelist_main = [
  # implicit linux ABI
    '_GLOBAL_OFFSET_TABLE_',
  # C89
    'calloc',
    'free',
    'malloc',
    'memcmp',
    'memcpy', '__memcpy_chk',
    'memmove', '__memmove_chk',
    'realloc',
    'sprintf', '__sprintf_chk',
    'strchr',
    'strcmp',
    'strlen',
    'strstr',
  # C99
    'snprintf', '__snprintf_chk',
    'vsnprintf', '__vsnprintf_chk',
    'abort',
  # kernel independent posix
    'strdup',
    'strndup',

  # debug code
    'printf', '__printf_chk', 'exit', 'fopen64', 'fputs', 'fputc', 'fclose',
]

# OS functions needed for integration component
import_whitelist_x = [
  # implicit linux ABI
    '__errno_location',
  # OS api
  # POSIX.1-2001 (or earlier)
    'clock_gettime',
    'close',
    'fcntl', 'fcntl64',
    'isatty',
    'open', 'open64',
    'poll',
    'read',
    'sigaction',
    'sigfillset',
    'tcgetattr',
    'tcsetattr',
    'write',
  # other
    'ioctl', # used with TIOCGWINSZ

  # rescue (POSIX.1-2001)
    'getenv',
    'sigprocmask',
    'select',
    'tcgetpgrp',
    'getpgrp',

  # TK based debug log window
    'environ',
    'pipe2',
    'posix_spawn_file_actions_addclose',
    'posix_spawn_file_actions_adddup2',
    'posix_spawn_file_actions_destroy',
    'posix_spawn_file_actions_init',
    'posix_spawnattr_destroy',
    'posix_spawnattr_getsigdefault',
    'posix_spawnattr_init',
    'posix_spawnattr_setflags',
    'posix_spawnp',
]

expected_leaked_private = [
  # used in development and automated testing, these are not supposed to used by any applications
    'termpaintp_test',              # entry point for testing
    'termpaintp_input_selfcheck',   # self check
    'termpaintp_input_dump_table',  # dumps input mapping table for fuzztesting dataset.
  # internal cross file imports
    'termpaintp_rescue_embedded',   # needed by termpaintx.c
]

class File:
    def __init__(self, name):
        self.name = name
        self.export_private = []
        self.export_api = []
        self.export_generic = []
        self.import_sibling = []
        self.import_system = []
        self.import_system_main = []
        self.import_system_x = []

def analyze_symbols(filename, prefix='termpaint_'):
    f = open(filename, 'rb')
    elffile = ELFFile(f)

    symbol_tables = [s for s in elffile.iter_sections() if isinstance(s, SymbolTableSection)]
    if len(symbol_tables) != 1:
        raise Exception("TODO handle multiple symbol tables")
    symbol_table = symbol_tables[0]

    file = File(filename)
    for nsym, symbol in enumerate(symbol_table.iter_symbols()):
        if symbol['st_info']['type'] in ('STT_FILE', 'STT_SECTION'):
            continue
        if symbol['st_info']['bind'] == 'STB_LOCAL':
            continue
        #print(symbol.name, symbol['st_other']['visibility'], symbol['st_info']['type'], symbol['st_shndx'])
        if isinstance(symbol['st_shndx'], int):
            if symbol.name.startswith('termpaintp'):
                file.export_private.append(symbol.name)
                # print('leaked private', symbol.name)
            elif symbol.name.startswith(prefix):
                file.export_api.append(symbol.name)
                # print('defined', symbol.name)
            else:
                file.export_generic.append(symbol.name)
                # print('leaked generic', symbol.name)
        elif symbol['st_shndx'] == 'SHN_UNDEF':
            if symbol.name.startswith('termpaint'):
                file.import_sibling.append(symbol.name)
                # print('import from sibling', symbol.name)
            else:
                file.import_system.append(symbol.name)
                # print('system import', symbol.name)

    file.import_system_main = [s for s in file.import_system if s in import_whitelist_main]
    file.import_system = [s for s in file.import_system if s not in import_whitelist_main]
    file.import_system_x = [s for s in file.import_system if s in import_whitelist_x]
    file.import_system = [s for s in file.import_system if s not in import_whitelist_x]
    return file

def print_violations(file):
    for sym in file.import_system:
        print('system import not on whitelist {}: {}'.format(file.name, sym))

    for sym in file.export_private:
        if sym in expected_leaked_private: continue
        print('leaked private symbol {}: {}'.format(file.name, sym))

    for sym in file.export_generic:
        print('leaked generic symbol {}: {}'.format(file.name, sym))

def main():
    mainlib = [
        analyze_symbols('_build/termpaint@sta/termpaint.c.o'),
        analyze_symbols('_build/termpaint@sta/termpaint_event.c.o'),
        analyze_symbols('_build/termpaint@sta/termpaint_input.c.o'),
    ]

    integration = [
        analyze_symbols('_build/termpaint@sta/termpaintx.c.o', prefix='termpaintx_'),
        analyze_symbols('_build/termpaint@sta/ttyrescue.c.o', prefix='termpaintp_rescue_embedded'),
    ]

    for file in mainlib:
        for sym in file.import_system_x:
            print('system import not on whitelist {}: {}'.format(file.name, sym))

        print_violations(file)

    for file in integration:
        print_violations(file)


if __name__ == '__main__':
    main()
