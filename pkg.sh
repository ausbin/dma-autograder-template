#!/bin/bash

rm -f assignment.tar.gz

pushd student
    make clean
    tar czhvf ../assignment.tar.gz *
popd
