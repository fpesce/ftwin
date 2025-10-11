#!/bin/bash

set -e

git submodule deinit -f third-party/libpuzzle || true
git rm -f third-party/libpuzzle || true
rm -rf .git/modules/third-party/libpuzzle
git submodule add https://github.com/jedisct1/libpuzzle third-party/libpuzzle
cd third-party/libpuzzle && git apply ../../patches/libpuzzle-fix-test.patch