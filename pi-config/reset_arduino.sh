#!/bin/bash
cd /sys/class/gpio/
echo 6 > export
echo out > gpio6/direction
echo 0 > gpio6/value
echo 1 > gpio6/value
echo 6 > unexport

