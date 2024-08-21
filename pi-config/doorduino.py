#!/usr/bin/python3

from serial.tools import list_ports
import serial
import io
import time
import subprocess
import csv
import getopt
import sys
import os
import math
import paramiko
from pathlib import Path
import configparser
import re
import threading
import syslog

def read_configuration(configdir):
    if (configdir == ''):
        print("Missing configdir.")
        sys.exit(2)

    if not os.path.exists(configdir):
        print("Directory ",  configdir, " does not exist");
        sys.exit(2)

    configfile = Path(os.path.join(configdir, 'settings'))
    if not configfile.is_file():
        print(configfile, " does not exist")
        sys.exit(2)

    config = configparser.ConfigParser()
    config.readfp(configfile.open())

    expected_config_options = { 'mqtt': [ 'doorbell.subject', 'dooropen.subject', 'lockstate.subject', 'server', 'mqtt-simple' ],
                                'serial': [ 'device' ] }

    for section, options in expected_config_options.items():
        for option in options:
            if not config.has_option(section, option):
                print("Missing config option ", option, "in section", section)
                sys.exit(2)

    return config

def mqtt_thread(config, subject, value, persistent):
    if persistent:
        subprocess.call([config.get('mqtt', 'mqtt-simple'), "-h", config.get('mqtt', 'server'), "-r", "-p", subject, "-m", value])
    else:
        subprocess.call([config.get('mqtt', 'mqtt-simple'), "-h", config.get('mqtt', 'server'), "-p", subject, "-m", value])

def mqtt(config, subject, value, persistent=False):
    threading.Thread(target = mqtt_thread, args = (config, subject, value, persistent)).start()

def log(message):
    print("LOG " + message)
    syslog.syslog(message)

def main(argv):
    global users

    configdir = ''

    syslog.openlog('doorduino')

    try:
        opts, args = getopt.getopt(argv,"c:",["config="])
    except getopt.GetoptError:
        print('doorduino.py -c <configdir>')
        sys.exit(2)

    for opt, arg in opts:
        if opt == "-c" or opt == "--config":
            configdir = arg

    config = read_configuration(configdir)

    while True:
        try:
            #cdc = next(list_ports.grep(config.get('serial', 'device')))
            #ser = serial.Serial(cdc[0])
            ser = serial.Serial(config.get('serial', 'device'), 115200)

            time.sleep(2);
#            ser.write(b"R\n");
            print("Doorduino started");

            while True:
                data = ser.readline()
                action = data.decode("iso-8859-1").strip()

                print("Data:" + action)
                if action == "Horn activated":
                    print("Horn activated")
                    log("Horn activated")
                    mqtt(config, config.get('mqtt','doorbell.subject'), '1', False)
                    time.sleep(2)
                    mqtt(config, config.get('mqtt','doorbell.subject'), '0', False)
                elif action == "Solenoid activated":
                    print("Solenoid activated")
                    log("Solenoid activated")
                    mqtt(config, config.get('mqtt','dooropen.subject'), '1', False)
                    time.sleep(2)
                    mqtt(config, config.get('mqtt','dooropen.subject'), '0', False)
                elif action == "iButton authenticated":
                    print("iButton authenticated")
                    log("iButton authenticated")
                elif action == "opening lock":
                    print("lock open")
                    log("lock open")
                    mqtt(config, config.get('mqtt','lockstate.subject'), 'open', True)
                elif action == "closing lock":
                    print("lock closed")
                    log("lock closed")
                    mqtt(config, config.get('mqtt','lockstate.subject'), 'closed', True)

#                if action == 'B':
#                    print("Arduino ready")
#                    ser.write(b"R\n")
#                elif action == 'A':
#                    print("Authenticating", value)
#                    try:
#                        if users[value]:
#                            ser.write(b"U");
#                            if users[value]['maintenance']:
#                                ser.write(b'T')
#                            else:
#                                ser.write(b'F')
#
#                            ser.write(users[value]['price'].encode('utf-8'))
#                            ser.write(users[value]['revbank'].encode('utf-8'))
#                            ser.write(b"\n")
#
#                            log("Laser unlock by " + users[value]['revbank'] + " (" + value + ")")
#
#                            active_user = users[value]
#                    except KeyError:
#                        log("Laser unlock attempt by unknown iButton " + value)
#                        ser.write(b"FNiet gevonden\n");
#                        threading.Thread(target = git_update, args = (config.get('git', 'git'), configdir)).start()
#
#                elif action == 'W':
#                    mqtt(config, config.get('mqtt','water.subject'), value, False);
#                elif action == 'T':
#                    mqtt(config, config.get('mqtt', 'temperature.subject'), value, False);
#                elif action == 'S':
#                    log("Laser job started for " + active_user['revbank'])
#                    mqtt(config, config.get('mqtt', 'laseractive.subject'), '1', True)
#                elif action == 'E':
#                    log("Laser job finished for " + active_user['revbank'] + " in " + value + "ms")
#                    mqtt(config, config.get('mqtt', 'laseractive.subject'), '0', True)
#                    payment(config, active_user, float(value))
#                elif action == 'M':
#                    log("Maintenance state entered by " + active_user['revbank'])
#                elif action == 'L':
#                    log("Left maintenance state by " + active_user['revbank'])

        except serial.SerialException:
            print("Serial connection error")
            time.sleep(2)
        except StopIteration:
            print("No device found")
            time.sleep(2)

if __name__ == "__main__":
    main(sys.argv[1:])

