#!/bin/bash


i2cset -y 1 0x48 2 0x1d
i2cset -y 1 0x48 3 0x1e

i2cset -y 1 0x4a 2 0x1d
i2cset -y 1 0x4a 3 0x1e
