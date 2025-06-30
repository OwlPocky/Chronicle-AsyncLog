#!/bin/bash
run-clang-tidy -p . -header-filter='.*' -checks='*,-llvmlibc-*,-google-*,-modernize-use-trailing-return-type'
