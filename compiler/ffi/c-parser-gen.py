#!/usr/bin/env python3

# Compiler for PHP (aka KPHP)
# Copyright (c) 2021 LLC «V Kontakte»
# Distributed under the GPL v3 License, see LICENSE.notice.txt

import argparse
import os
import subprocess
import shutil

FILE_DIR = os.path.dirname(os.path.realpath(__file__))

def prepare_output_dir(auto_dir):
    output_dir = os.path.join(auto_dir, 'compiler', 'ffi', 'c_parser')
    if os.path.exists(output_dir):
        shutil.rmtree(output_dir)
    os.makedirs(output_dir)
    return output_dir

def generate_parser(output_dir):
    output_file = os.path.join(output_dir, 'yy_parser.cpp')
    grammar_file = os.path.join(FILE_DIR, 'c_parser', 'c.y')
    cmd = ['bison', '-o', output_file, grammar_file]
    ret_code = subprocess.call(cmd)
    if ret_code != 0:
        raise Exception("failed to generate parser (" + str(cmd) + ")")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--auto', required=True, help='path to auto directory')
    args = parser.parse_args()
    output_dir = prepare_output_dir(args.auto)
    generate_parser(output_dir)
