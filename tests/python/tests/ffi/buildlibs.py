# -*- coding: utf-8 -*-

# Compiler for PHP (aka KPHP)
# Copyright (c) 2021 LLC «V Kontakte»
# Distributed under the GPL v3 License, see LICENSE.notice.txt

import os
import subprocess

def compile_shared_lib(test_dir, name):
    obj_file_name = os.path.join(test_dir, name + '.o')
    c_file_name = os.path.join(test_dir, 'c', name + '.c')
    lib_file_name = os.path.join(test_dir, 'lib' + name + '.so')
    cmd = ['gcc', '-c', '-fpic', '-o', obj_file_name, c_file_name]
    ret_code = subprocess.call(cmd, cwd=test_dir)
    if ret_code != 0:
        raise Exception("failed to compile " + c_file_name)
    cmd = ['gcc', '-shared', '-o', lib_file_name, obj_file_name]
    ret_code = subprocess.call(cmd, cwd=test_dir)
    if ret_code != 0:
        raise Exception("failed to create a shared lib " + lib_file_name)

def main():
    shared_libs = ['vector', 'pointers']
    for lib in shared_libs:
        compile_shared_lib(os.path.dirname(os.path.realpath(__file__)), lib)

main()
