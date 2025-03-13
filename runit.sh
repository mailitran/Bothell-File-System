#!/bin/bash

rm -f a.out

# Overwrites contents of BFSDISK with a clean copy
cp BFSDISK-clean-backup BFSDISK

gcc -Wall -Wextra -Wno-sign-compare *.c

#./a.out
