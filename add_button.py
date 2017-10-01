#!/usr/bin/python3
import serial
import time
import sys

def list_buttons(ser):
    ser.write(b'\r\n')
    time.sleep(0.1)
    ser.write(b'list_buttons\r\n')

    expect_button_line = False
    buttons_in_arduino = []
    while True:
        line = ser.readline()
        if line.strip() == b"button list start":
            expect_button_line = True
            continue

        if expect_button_line and line.startswith(b"button: "):
            key, val = line.strip().split(b" ")
            buttons_in_arduino.append(val)
        if line == b"":
            break
    buttons_in_arduino.sort()
    print(len(buttons_in_arduino))
    return buttons_in_arduino

def remove_button(ser, button):
    ser.write(b'\r\n')
    time.sleep(0.1)
    ser.write(b'remove_button '+ button + b'\r\n')
    while True:
        line = ser.readline()
        if line == b"":
            break
        print(line)

def add_button(ser, button, secret):
    ser.write(b'\r\n')
    time.sleep(0.1)
    ser.write(b'add_button '+ button + b' ' + secret + b'\r\n')
    while True:
        line = ser.readline()
        if line == b"":
            break
        print(line)

if len(sys.argv) < 3:
    print("Usage: add_button button secret")
    sys.exit(1)

for tty in [ "/dev/ttyS1", "/dev/ttyS2" ]:
    with serial.Serial(tty, 115200, timeout=1) as ser:
        add_button(ser, sys.argv[1], sys.argv[2])

        print(list_buttons(ser))
