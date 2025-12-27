#!/bin/bash
cd edge && make clean && make
cd ../cloud && make clean && make
