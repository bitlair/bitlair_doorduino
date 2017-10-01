#!/usr/bin/env python3

import serial
import sys
import time

SERIAL_PORTS = [ '/dev/ttyS1', '/dev/ttyS2' ]


def open_port(tty):
   return serial.Serial(tty, 115200, timeout=1)

def list_buttons(ports):
    ids_per_port = {}
    for port in ports:
        with open_port(port) as ser:
            ser.write(b'\r\n')
            time.sleep(0.1)
            ser.write(b'list_buttons\r\n')

            expect_button_line = False
            buttons_in_arduino = []
            while True:
                line = ser.readline()
                if line.strip() == b'button list start':
                    expect_button_line = True
                    continue

                if expect_button_line and line.startswith(b'button: '):
                    key, val = line.strip().split(b' ')
                    buttons_in_arduino.append(val)
                if line == b'':
                    break
            buttons_in_arduino.sort()
            ids_per_port[port] = buttons_in_arduino

    print('| '+' | '.join([ port.ljust(16) for port in ids_per_port.keys() ])+' |')
    print('| '+' | '.join([ ('len = %d' % len(ls)).ljust(16) for ls in ids_per_port.values() ])+' |')
    for i in range(max([ len(ls) for ls in ids_per_port.values() ])):
        ids = [
            ids_per_port[port][i].decode("ascii") if i < len(ids_per_port[port]) else ''
            for port in ports
        ]
        print('| '+' | '.join(ids)+' |')


def remove_button(ports, button):
    for port in ports:
        with open_port(port) as ser:
            ser.write(b'\r\n')
            time.sleep(0.1)
            ser.write(b'remove_button ' + button.encode("ascii") + b'\r\n')
            while True:
                line = ser.readline()
                if line == b'':
                    break
                print(line)

def add_button(ser, button, secret):
    for port in ports:
        with open_port(port) as ser:
            ser.write(b'\r\n')
            time.sleep(0.1)
            ser.write(b'add_button ' + button.encode("ascii") + b' ' + secret.encode("ascii") + b'\r\n')
            while True:
                line = ser.readline()
                if line == b'':
                    break
                print(line)

def print_help(trailer=None):
    print(
'''Usage: %s <action>

Actions:
    list
    add <id>:<secret>
    remove <id>
''' % sys.argv[0])
    if trailer is not None:
        print(trailer)
    sys.exit(1)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print_help()

    action = sys.argv[1]
    if action == 'list':
        list_buttons(SERIAL_PORTS)

    elif action == 'add':
        if len(sys.argv) < 3:
            print_help('Missing button id and secret')
        keypair = sys.argv[2].split(':')
        add_button(SERIAL_PORTS, keypair[0], keypair[1])

    elif action == 'remove':
        if len(sys.argv) < 3:
            print_help('Missing button id')
        remove_button(SERIAL_PORTS, sys.argv[2])

    else:
        print_help()
