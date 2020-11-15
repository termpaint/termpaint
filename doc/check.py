#! /usr/bin/python3

import json
import sys
import os

d = json.load(open('_build/json/genindex.fjson', 'r'))
c_functions = []
c_macros = []
c_types = []
c_enums = []
for letter in d['genindexentries']:
    for entry in letter[1]:
        display_name = entry[0]

        if display_name.endswith(' (C function)'):
            c_functions.append(display_name[:-13])
        elif display_name.endswith(' (C macro)'):
            c_macros.append(display_name[:-10])
        elif display_name.endswith(' (C type)'):
            c_types.append(display_name[:-9])
        elif display_name.endswith(' (C enum)'):
            c_enums.append(display_name[:-9])
        elif display_name.endswith(' (C enumerator)'):
            pass # ignore
        else:
            print("can not guess type of index entry: ", display_name)

# keys
c_functions.extend(['termpaint_input_enter', 'termpaint_input_space', 'termpaint_input_tab', 'termpaint_input_backspace', 'termpaint_input_context_menu', 'termpaint_input_delete', 'termpaint_input_end', 'termpaint_input_home', 'termpaint_input_insert', 'termpaint_input_page_down', 'termpaint_input_page_up', 'termpaint_input_arrow_down', 'termpaint_input_arrow_left', 'termpaint_input_arrow_right', 'termpaint_input_arrow_up', 'termpaint_input_numpad_divide', 'termpaint_input_numpad_multiply', 'termpaint_input_numpad_subtract', 'termpaint_input_numpad_add', 'termpaint_input_numpad_enter', 'termpaint_input_numpad_decimal', 'termpaint_input_numpad0', 'termpaint_input_numpad1', 'termpaint_input_numpad2', 'termpaint_input_numpad3', 'termpaint_input_numpad4', 'termpaint_input_numpad5', 'termpaint_input_numpad6', 'termpaint_input_numpad7', 'termpaint_input_numpad8', 'termpaint_input_numpad9', 'termpaint_input_escape', 'termpaint_input_f1', 'termpaint_input_f2', 'termpaint_input_f3', 'termpaint_input_f4', 'termpaint_input_f5', 'termpaint_input_f6', 'termpaint_input_f7', 'termpaint_input_f8', 'termpaint_input_f9', 'termpaint_input_f10', 'termpaint_input_f11', 'termpaint_input_f12'])
c_functions.extend([]) # intentionally undocumented
c_types.extend(['termpaint_integration_private']) # intentionally undocumented
c_types.extend(['termpaint_logging_func']) # helper to workaround problems in inline type specification

sys.path.insert(0, "/opt/qtcreator-build/llvm-10/clang/bindings/python/")
os.environ['CLANG_LIBRARY_PATH'] = '/opt/qtcreator-build/llvm-10-build/lib'

from clang.cindex import CursorKind
from clang.cindex import Index, TranslationUnit
from clang.cindex import SourceLocation, SourceRange
from clang.cindex import TokenKind, TokenGroup

import clang.cindex

clang.cindex.Config.set_library_path('/opt/qtcreator-build/llvm-10-build/lib')

function_names_from_source = []
typedef_names_from_source = []
enum_names_from_source = []
macro_names_from_source = []

def get_from_header(header_name):
    expected_basename = header_name.split('/')[-1]

    index = Index.create()

    tu = index.parse(header_name, options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
    #  | TranslationUnit.PARSE_SKIP_FUNCTION_BODIES
    for cursor in tu.cursor.get_children():
        #print(cursor.kind)
        basename = str(cursor.location.file).split('/')[-1]
        if basename != expected_basename: continue
        #print(cursor.kind, cursor.displayname)
        if cursor.kind == CursorKind.FUNCTION_DECL:
            function_names_from_source.append(cursor.spelling)
            sig = cursor.result_type.spelling + ' ' + cursor.spelling + '('
            args = []
            for arg in cursor.get_children():
               if arg.kind != CursorKind.PARM_DECL: continue
               args.append(arg.type.spelling + ' ' + arg.spelling)
            #if cursor.type.is_function_variadic():
            #    args.append('...')
            sig += ', '.join(args) + ')'
            #print(sig)
        if cursor.kind == CursorKind.TYPEDEF_DECL:
            typedef_names_from_source.append(cursor.spelling)
        if cursor.kind == CursorKind.ENUM_DECL:
            enum_names_from_source.append(cursor.spelling)
        if cursor.kind == CursorKind.MACRO_DEFINITION and not cursor.spelling.endswith('_INCLUDED'):
            macro_names_from_source.append(cursor.spelling)


get_from_header('../termpaint.h')
get_from_header('../termpaint_event.h')
get_from_header('../termpaint_image.h')
get_from_header('../termpaint_input.h')
get_from_header('../termpaintx.h')
get_from_header('../termpaintx_ttyrescue.h')

def filter(fn):
    return (   (fn.endswith('_mustcheck') and fn[:-10] in function_names_from_source)
            or (fn.endswith('_or_nullptr') and fn[:-11] in function_names_from_source))

macro_names_from_source = [name for name in macro_names_from_source if name not in ('_tERMPAINT_PUBLIC', 'TERMPAINTP_CAST')]

function_names_from_source = [fn for fn in function_names_from_source if not filter(fn)]

#print("undocumented functions: ", set(function_names_from_source) - set(c_functions))
#print("undocumented macros: ", set(macro_names_from_source) - set(c_macros))
#print("undocumented types: ", set(typedef_names_from_source) - set(c_types))
#print("enums: ", enum_names_from_source)

#print("number of items: ", len(function_names_from_source) + len(macro_names_from_source) + len(typedef_names_from_source) + len(enum_names_from_source))

for i in (list(set(function_names_from_source) - set(c_functions))
        + list(set(macro_names_from_source) - set(c_macros))
        + list(set(typedef_names_from_source) - set(c_types))
        + list(set(enum_names_from_source) - set(c_enums))):
    print(i)
