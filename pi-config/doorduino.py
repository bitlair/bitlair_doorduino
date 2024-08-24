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
import csv

import logging
from paho.mqtt import client as mqtt_client

def connect_mqtt(config, broker, port, client_id):
    # def on_connect(client, userdata, flags, rc):
    # For paho-mqtt 2.0.0, you need to add the properties parameter.
    def on_connect(client, userdata, flags, rc, properties):
        if rc == 0:
            print("Connected to MQTT Broker!")
            subscribe(client, config.get('mqtt', 'state-bitlair.subject'))
            subscribe(client, config.get('mqtt', 'state-djo.subject'))
        else:
            print("Failed to connect, return code %d\n", rc)
    # Set Connecting Client ID
    # For paho-mqtt 2.0.0, you need to set callback_api_version.
    client = mqtt_client.Client(client_id=client_id, callback_api_version=mqtt_client.CallbackAPIVersion.VERSION2)

    # client.username_pw_set(username, password)
    client.on_connect = on_connect
    client.connect(broker, port)
    return client

FIRST_RECONNECT_DELAY = 1
RECONNECT_RATE = 2
MAX_RECONNECT_COUNT = 12
MAX_RECONNECT_DELAY = 60

def on_disconnect(client, userdata, rc):
    logging.info("Disconnected with result code: %s", rc)
    reconnect_count, reconnect_delay = 0, FIRST_RECONNECT_DELAY
    while reconnect_count < MAX_RECONNECT_COUNT:
        logging.info("Reconnecting in %d seconds...", reconnect_delay)
        time.sleep(reconnect_delay)

        try:
            client.reconnect()
            logging.info("Reconnected successfully!")
            return
        except Exception as err:
            logging.error("%s. Reconnect failed. Retrying...", err)

        reconnect_delay *= RECONNECT_RATE
        reconnect_delay = min(reconnect_delay, MAX_RECONNECT_DELAY)
        reconnect_count += 1
    logging.info("Reconnect failed after %s attempts. Exiting...", reconnect_count)

states = {}
def update_spacestate():
    global ser
    open = False
    for key in states:
        if states[key] == "open":
            open = True
    try:
        if open:
            print("Send spacestate open")
            ser.write(b"\n")
            ser.write(b"spacestate open\n");
        else:
            print("Send spacestate closed")
            ser.write(b"\n")
            ser.write(b"spacestate closed\n")
    except serial.SerialException:
        print("Serial connection error")
        time.sleep(2)


def subscribe(client: mqtt_client, topic):
    def on_message(client, userdata, msg):
        states[msg.topic] = msg.payload.decode()
        print(f"Received `{msg.payload.decode()}` from `{msg.topic}` topic")
        update_spacestate()

    client.subscribe(topic)
    client.on_message = on_message


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
    config.read_file(configfile.open())

    expected_config_options = { 'mqtt': [ 'doorbell.subject', 'dooropen.subject', 'lockstate.subject', 'server', 'mqtt-simple' ],
                                'serial': [ 'device' ] }

    for section, options in expected_config_options.items():
        for option in options:
            if not config.has_option(section, option):
                print("Missing config option ", option, "in section", section)
                sys.exit(2)

    return config

def mqtt_send_thread(config, subject, value, persistent):
    if persistent:
        subprocess.call([config.get('mqtt', 'mqtt-simple'), "-h", config.get('mqtt', 'server'), "-r", "-p", subject, "-m", value])
    else:
        subprocess.call([config.get('mqtt', 'mqtt-simple'), "-h", config.get('mqtt', 'server'), "-p", subject, "-m", value])

def mqtt(config, subject, value, persistent=False):
    threading.Thread(target = mqtt_send_thread, args = (config, subject, value, persistent)).start()

def log(message):
    print("LOG " + message)
    syslog.syslog(message)

def mqtt_thread():
    global client
    global config
    client = connect_mqtt(config, config.get('mqtt', 'server'), 1883, config.get('mqtt', 'client-id'))

    client.loop_forever()

buttons = []
def serial_monitor_thread():
    global ser
    global config
    global buttons
    while True:
        try:
            ser = serial.Serial(config.get('serial', 'device'), 115200, rtscts=False, dsrdtr=True)

            time.sleep(2);
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
                elif action[:7] == "button:":
                    # print("got button ")
                    # print(action[8:])
                    if not action[8:] in buttons:
                        buttons.append(action[8:])
                elif action == "DEBUG: Board started":
                    print("Arduino was reset, sending spacestate")
                    update_spacestate()

        except serial.SerialException:
            print("Serial connection error")
            time.sleep(2)
        except StopIteration:
            print("No device found")
            time.sleep(2)


def git_update(git_binary, git_dir):
    log("Updating git")
    subprocess.call([git_binary, "-C", git_dir, "pull"])


def git_thread():
    global ser
    global buttons
    print("GIT init")
    git_update("git", "toegang")
    git_buttons = {}
    with open('toegang/toegang.csv', newline='') as csvfile:
        data = csvfile.read()
        data = data.replace(' ', '')
        reader = csv.DictReader(data.splitlines(), delimiter=',')
        for row in reader:
            ibutton = row['ibutton'].split(':')
            git_buttons[ibutton[0].lower()] = ibutton[1].lower()
            # print(row['naam'] + "   " + row['ibutton'])

    if len(git_buttons) < 25:
        print("Something wrong, not enough buttons in git")
        return

    buttons = []
    ser.write(b"\n")
    ser.write(b"list_buttons\n");
    time.sleep(10)
    if len(buttons) < 5:
        print("Something wrong, not enough buttons in doorduino")
        return

    print(buttons)
    for button in git_buttons:
        if button not in buttons:
            print("should add " + button)
            ser.write(b"\n")
            ser.write(b"add_button "+button.encode('ascii')+b" "+git_buttons[button].encode('ascii')+b"\n")
            time.sleep(2)
        # else:
        #     print("already there " + button)

    for button in buttons:
        if button not in git_buttons:
            print("should remove " + button)
            ser.write(b"\n")
            ser.write(b"remove_button "+button.encode('ascii')+b"\n")
            time.sleep(2)
        # else:
        #     print("should be there " + button)



def threadwrap(threadfunc):
    def wrapper():
        while True:
            try:
                threadfunc()
            except BaseException as e:
                print('{!r}; restarting thread'.format(e))
            else:
                print('exited normally, bad thread; restarting')
    return wrapper


def main(argv):
    global config
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

    threading.Thread(target = threadwrap(serial_monitor_thread)).start()
    time.sleep(5) # Give doorduino time to start before sending spacestate data
    threading.Thread(target = threadwrap(mqtt_thread)).start()
    # threading.Thread(target = threadwrap(git_thread)).start()
    git_thread()


if __name__ == "__main__":
    main(sys.argv[1:])

