#! /bin/bash

find components -iname "*.h*" -o -iname "*.c*"  | xargs clang-format -i
