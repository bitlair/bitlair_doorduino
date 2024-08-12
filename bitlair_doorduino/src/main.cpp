#include "OneWire.h"
#include "ds1961.h"

#include <stdint.h>
#include <string.h>
#include <EEPROM.h>
#include "Entropy.h"
#include "sha1.h"


#include <Arduino.h>
// Motor steps per revolution. Most steppers are 200 steps or 1.8 degrees/step
#define MOTOR_STEPS            100
#define RPM                    120
#define DIR                    A0
#define STEP                   9
#include "A4988.h"
A4988 stepper(MOTOR_STEPS, DIR, STEP);

#define INPUT_SOLENOID         4
#define INPUT_HORN             3
#define PIN_LEDSOLENOID        6
#define PIN_LEDHORN            5
bool    StateSolenoid = false;
bool    StateHorn = false;
uint32_t SolenoidStartTime;



#define PIN_DOORPOWER          A3
#define PIN_SOLENOID           A5
#define PIN_HORN               A4
#define PIN_OPEN               8
#define PIN_CLOSE              7

#define PIN_1WIRE              13
#define PIN_LEDGREEN           10
#define PIN_LEDRED             11

#define PIN_MAINS_POWER        A2

#define CMD_BUFSIZE            64
#define CMD_TIMEOUT            10000 //command timeout in milliseconds

#define SECRETSIZE             8
#define ADDRSIZE               8
#define STORAGESIZE            (SECRETSIZE + ADDRSIZE)
#define EEPROMSIZE             1024
#define SHA1SIZE               20

#define IBUTTON_SEARCH_TIMEOUT 60000 //timeout searching for ibutton

#define LEDState_Off         0
#define LEDState_Reading     1
#define LEDState_Authorized  2
#define LEDState_Busy        3

#define htons(x) ( ((x)<<8) | (((x)>>8)&0xFF) )
#define ntohs(x) htons(x)

#define htonl(x) ( ((x)<<24 & 0xFF000000UL) | \
                   ((x)<< 8 & 0x00FF0000UL) | \
                   ((x)>> 8 & 0x0000FF00UL) | \
                   ((x)>>24 & 0x000000FFUL) )
#define ntohl(x) htonl(x)

OneWire ds(PIN_1WIRE);
DS1961  ibutton(&ds);

bool HasMainsPower();

int Serialprintf (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));

int Serialprintf (const char* fmt, ...)
{
  char buf[256];

  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  Serial.print(buf);
}

uint8_t  g_ledstate = LEDState_Off;
uint32_t g_ledtimestart;
bool     g_fade;
bool     g_lockopen;

#define LED_PERIOD 1024

void ProcessLEDs()
{
  if (g_ledstate == LEDState_Off)
  {
    analogWrite(PIN_LEDGREEN, 0);
    analogWrite(PIN_LEDRED, 0);
  }
  else if (g_ledstate == LEDState_Reading)
  {
    uint32_t timesincesetting = millis() + LED_PERIOD / 2 - g_ledtimestart;
    uint32_t ledtime = timesincesetting % LED_PERIOD;
    uint32_t ledval;
    if (ledtime < LED_PERIOD / 2)
      ledval = ledtime;
    else
      ledval = (LED_PERIOD - 1) - ledtime;

    ledval = (ledval * ledval) / LED_PERIOD;
    if (!HasMainsPower())
      ledval = (ledval + 5) / 10;

    if (g_lockopen)
    {
      analogWrite(PIN_LEDGREEN, ledval);
      analogWrite(PIN_LEDRED, 0);
    }
    else
    {
      analogWrite(PIN_LEDGREEN, 0);
      analogWrite(PIN_LEDRED, ledval);
    }
  }
  else if (g_ledstate == LEDState_Authorized)
  {
    if (g_lockopen)
    {
      analogWrite(PIN_LEDGREEN, 255);
      analogWrite(PIN_LEDRED, 0);
    }
    else
    {
      analogWrite(PIN_LEDGREEN, 0);
      analogWrite(PIN_LEDRED, 255); 
    }
  }
  else if (g_ledstate == LEDState_Busy)
  {
    analogWrite(PIN_LEDGREEN, 255);
    analogWrite(PIN_LEDRED, 255);
  }
}

void SetLEDState(uint8_t ledstate)
{
  if (ledstate == LEDState_Reading && g_ledstate != LEDState_Reading)
  {
    g_ledtimestart = millis();
    g_fade = true;
  }

  g_ledstate = ledstate;

  ProcessLEDs();
}

void DelayLEDs(uint32_t delayms)
{
  uint32_t now = millis();
  while (millis() - now < delayms)
    ProcessLEDs();
}

void setup()
{
  Serial.begin(115200);
  Serial.println("DEBUG: Board started");

  stepper.begin(RPM);
  stepper.enable();
  stepper.setMicrostep(1);  // Set microstep mode to 1:1

  pinMode(INPUT_SOLENOID, INPUT_PULLUP);
  pinMode(INPUT_HORN, INPUT_PULLUP);
  pinMode(PIN_LEDSOLENOID, OUTPUT);
  pinMode(PIN_LEDHORN, OUTPUT);
  pinMode(PIN_DOORPOWER, OUTPUT);
  pinMode(PIN_SOLENOID, OUTPUT);
  pinMode(PIN_OPEN, OUTPUT);
  pinMode(PIN_CLOSE, OUTPUT);
  pinMode(PIN_HORN, OUTPUT);

  pinMode(PIN_LEDGREEN, OUTPUT);
  pinMode(PIN_LEDRED, OUTPUT);
  pinMode(PIN_MAINS_POWER, INPUT);

  digitalWrite(PIN_OPEN, LOW);
  digitalWrite(PIN_CLOSE, LOW);
  digitalWrite(PIN_DOORPOWER, LOW);

  SetLEDState(LEDState_Off);

  Entropy.initialize();
}

void AddButton(uint8_t* addr, uint8_t* secret)
{
  for (uint16_t i = 0; i < EEPROMSIZE / STORAGESIZE; i++)
  {
    bool emptyslot = true;
    uint16_t startaddr = i * STORAGESIZE;
    for (uint16_t j = 0; j < ADDRSIZE; j++)
    {
      uint8_t eeprombyte = EEPROM.read(startaddr + j);
      if (eeprombyte != 0xFF && eeprombyte != addr[j])
      {
        emptyslot = false;
        break;
      }
    }

    if (emptyslot)
    {
      for (uint16_t j = 0; j < ADDRSIZE; j++)
        EEPROM.write(startaddr + j, addr[j]);

      for (uint16_t j = 0; j < SECRETSIZE; j++)
        EEPROM.write(startaddr + j + ADDRSIZE, secret[j]);

      Serialprintf("DEBUG: stored button in slot %i\n", i);

      return;
    }
  }

  Serial.println("ERROR: no room in eeprom to store button");
}

void RemoveButton(uint8_t* addr)
{
  for (uint16_t i = 0; i < EEPROMSIZE / STORAGESIZE; i++)
  {
    uint16_t startaddr = i * STORAGESIZE;
    bool sameaddr = true;
    for (uint16_t j = 0; j < ADDRSIZE; j++)
    {
      uint8_t eeprombyte = EEPROM.read(startaddr + j);
      if (eeprombyte != addr[j])
      {
        sameaddr = false;
        break;
      }
    }
    if (!sameaddr)
      continue;

    Serialprintf("DEBUG: erasing slot %i\n", i);

    for (uint16_t j = 0; j < STORAGESIZE; j++)
      EEPROM.write(startaddr + j, 0xFF);
  }
}

bool GetButtonSecret(uint8_t* addr, uint8_t* secret)
{
  for (uint16_t i = 0; i < EEPROMSIZE / STORAGESIZE; i++)
  {
    uint16_t startaddr = i * STORAGESIZE;
    bool sameaddr = true;
    bool isempty = true;
    for (uint16_t j = 0; j < ADDRSIZE; j++)
    {
      uint8_t eeprombyte = EEPROM.read(startaddr + j);
      if (isempty && eeprombyte != 0xFF)
        isempty = false;

      if (eeprombyte != addr[j])
      {
        sameaddr = false;
        break;
      }
    }

    if (isempty)
      continue;

    if (sameaddr)
    {
      Serialprintf("DEBUG: getting secret from slot %i\n", i);

      for (uint16_t j = 0; j < SECRETSIZE; j++)
        secret[j] = EEPROM.read(startaddr + j + ADDRSIZE);

      return true;
    }
  }

  Serial.println("DEBUG: can't find secret for button");

  return false;
}

void ListButtons()
{
  Serial.println("button list start");

  for (uint16_t i = 0; i < EEPROMSIZE / STORAGESIZE; i++)
  {
    uint16_t startaddr = i * STORAGESIZE;
    uint8_t  buttonid[ADDRSIZE];
    bool     isempty = true;
    for (uint16_t j = 0; j < ADDRSIZE; j++)
    {
      uint8_t eeprombyte = EEPROM.read(startaddr + j);
      if (isempty && eeprombyte != 0xFF)
        isempty = false;

      buttonid[j] = eeprombyte;
    }

    if (isempty)
      continue;

    Serialprintf("button: ");
    for (uint16_t j = 0; j < ADDRSIZE; j++)
      Serialprintf("%02x", buttonid[j]);
    Serialprintf("\n");
  }
}

#define RANDOMDELAY_MIN  50
#define RANDOMDELAY_MAX 200

bool AuthenticateButton(uint8_t* addr)
{
  uint8_t secret[SECRETSIZE];
  if (!GetButtonSecret(addr, secret))
    return false;

  uint8_t mac_from_ibutton[SHA1SIZE];
  uint8_t data[32];
  uint8_t nonce[3];

  for (uint8_t i = 0; i < sizeof(nonce); i++)
    nonce[i] = Entropy.randomByte();

  if (!ibutton.ReadAuthWithChallenge(addr, 0, nonce, data, mac_from_ibutton))
    return false;

  sha1::sha1nfo sha1data = {};
  sha1::sha1_init(&sha1data);
  sha1::sha1_write(&sha1data, (const char*)secret, 4);
  sha1::sha1_write(&sha1data, (const char*)data, sizeof(data));
  sha1::sha1_writebyte(&sha1data, 0xff);
  sha1::sha1_writebyte(&sha1data, 0xff);
  sha1::sha1_writebyte(&sha1data, 0xff);
  sha1::sha1_writebyte(&sha1data, 0xff);
  sha1::sha1_writebyte(&sha1data, 0x40);
  sha1::sha1_write(&sha1data, (const char*)addr, ADDRSIZE - 1);
  sha1::sha1_write(&sha1data, (const char*)secret + 4, 4);
  sha1::sha1_write(&sha1data, (const char*)nonce, sizeof(nonce));

  uint8_t* sha_computed = sha1::sha1_result(&sha1data);

  uint8_t mac_computed[SHA1SIZE];
  ((uint32_t*)mac_computed)[0] = htonl(ntohl(*(uint32_t *)sha_computed) - 0x67452301);
  ((uint32_t*)mac_computed)[1] = htonl(ntohl(*(uint32_t *)(sha_computed+4)) - 0xefcdab89);
  ((uint32_t*)mac_computed)[2] = htonl(ntohl(*(uint32_t *)(sha_computed+8)) - 0x98badcfe);
  ((uint32_t*)mac_computed)[3] = htonl(ntohl(*(uint32_t *)(sha_computed+12)) - 0x10325476);
  ((uint32_t*)mac_computed)[4] = htonl(ntohl(*(uint32_t *)(sha_computed+16)) - 0xc3d2e1f0);

  //this check should always take the same amount of time, to prevent a timing attack
  bool macvalid = true;
  for (uint8_t i = 0; i < SHA1SIZE; i++)
  {
    if (mac_from_ibutton[i] != mac_computed[SHA1SIZE - 1 - i])
      macvalid = false;
  }

  //add a random delay
  delayMicroseconds(Entropy.random(RANDOMDELAY_MIN, RANDOMDELAY_MAX));

  return macvalid;
}

bool ReadCMD(char* cmdbuf, uint8_t* cmdbuffill)
{
  uint32_t cmdstarttime = millis();
  *cmdbuffill = 0;
  for(;;)
  {
    if (Serial.available())
    {
      char input = Serial.read();
      if (input == '\n')
      {
        cmdbuf[*cmdbuffill] = 0;
        return true;
      }
      else if (*cmdbuffill < CMD_BUFSIZE - 1)
      {
        cmdbuf[*cmdbuffill] = input;
        (*cmdbuffill)++;
      }
    }

    if (millis() - cmdstarttime >= CMD_TIMEOUT)
    {
      Serial.println("ERROR: timeout receiving command");
      return false;
    }
  }
}

uint8_t NextWordPos(char* cmdbuf, uint8_t cmdbuffill, uint8_t index)
{
  bool foundwhitespace = false;
  for (uint8_t i = index; i < cmdbuffill; i++)
  {
    if (!foundwhitespace)
    {
      if (cmdbuf[i] == ' ')
        foundwhitespace = true;
    }
    else
    {
      if (cmdbuf[i] != ' ')
      {
        return i;
      }
    }
  }

  return 0;
}

bool GetHexWordFromCMD(char* cmdbuf, uint8_t cmdbuffill, uint8_t* wordpos, uint8_t* wordbuf, uint8_t wordsize, char* wordname)
{
  *wordpos = NextWordPos(cmdbuf, cmdbuffill, *wordpos);

  if (*wordpos == 0)
  {
    Serialprintf("ERROR: no %s found in command\n", wordname);
    return false;
  }
  else if (cmdbuffill - *wordpos < wordsize * 2)
  {
    Serialprintf("ERROR: %s is too short\n", wordname);
    return false;
  }

  for (uint8_t i = 0; i < wordsize; i++)
  {
    if ((cmdbuf[*wordpos + i * 2] == ' ') || (cmdbuf[*wordpos + i * 2 + 1] == ' '))
    {
      Serialprintf("ERROR: %s is too short\n", wordname);
      return false;
    }

    int numread = sscanf(cmdbuf + *wordpos + i * 2, "%2hhx", wordbuf + i);
    if (numread != 1)
    {
      Serialprintf("ERROR: %s is invalid\n", wordname);
      return false;
    }
  }

  return true;
}

#define CMD_ADD_BUTTON    "add_button"
#define CMD_REMOVE_BUTTON "remove_button"
#define CMD_LIST_BUTTONS "list_buttons"

void ParseCMD(char* cmdbuf, uint8_t cmdbuffill)
{
  Serial.print("DEBUG: Received cmd: ");
  Serial.println(cmdbuf);

  bool isadd = strncmp(CMD_ADD_BUTTON, cmdbuf, strlen(CMD_ADD_BUTTON)) == 0;
  bool isremove = strncmp(CMD_REMOVE_BUTTON, cmdbuf, strlen(CMD_REMOVE_BUTTON)) == 0;
  bool islist = strncmp(CMD_LIST_BUTTONS, cmdbuf, strlen(CMD_LIST_BUTTONS)) == 0;

  if (isadd || isremove)
  {
    uint8_t wordpos = 0;
    uint8_t addr[ADDRSIZE];
    if (!GetHexWordFromCMD(cmdbuf, cmdbuffill, &wordpos, addr, ADDRSIZE, "address"))
      return;

    Serial.print("DEBUG: Received address ");
    for (uint8_t i = 0; i < ADDRSIZE; i++)
      Serialprintf("%02x", addr[i]);
    Serial.print("\n");

    bool addrvalid = false;
    for (uint8_t i = 0; i < ADDRSIZE; i++)
    {
      if (addr[i] != 0xFF)
      {
        addrvalid = true;
        break;
      }
    }
    if (!addrvalid)
    {
      Serial.println("ERROR: address FFFFFFFFFFFFFFFF is invalid");
      return;
    }

    if (isadd)
    {
      uint8_t secret[SECRETSIZE];
      if (!GetHexWordFromCMD(cmdbuf, cmdbuffill, &wordpos, secret, SECRETSIZE, "secret"))
        return;

      Serial.print("DEBUG: Received secret ");
      for (uint8_t i = 0; i < ADDRSIZE; i++)
        Serialprintf("%02x", secret[i]);
      Serial.print("\n");

      AddButton(addr, secret);
    }
    else
    {
      Serialprintf("DEBUG: removing button\n");
      RemoveButton(addr);
    }
  }
  else if (islist)
  {
    ListButtons();
  }
  else
  {
    Serial.println("Unknown command");
  }
}

#define TOGGLE_TIME 2500
#define BUTTON_TIME 250

void ToggleLock()
{
  if (g_lockopen)
  {
    g_lockopen = false;
    Serial.println("closing lock");
    digitalWrite(PIN_DOORPOWER, HIGH);
    digitalWrite(PIN_CLOSE, HIGH);
    DelayLEDs(BUTTON_TIME);
    DelayLEDs(TOGGLE_TIME - BUTTON_TIME);
  }
  else
  {
    g_lockopen = true;
    Serial.println("opening lock");
    digitalWrite(PIN_DOORPOWER, HIGH);
    digitalWrite(PIN_OPEN, HIGH);
    DelayLEDs(BUTTON_TIME);
    DelayLEDs(TOGGLE_TIME - BUTTON_TIME);
  }

  DelayLEDs(4000);
  digitalWrite(PIN_OPEN, LOW);
  digitalWrite(PIN_CLOSE, LOW);
  digitalWrite(PIN_DOORPOWER, LOW);

  Serial.println("finished lock action");
}

bool HasMainsPower()
{
  return digitalRead(PIN_MAINS_POWER) == HIGH;
}

void loop()
{
  uint8_t addr[ADDRSIZE];
  uint32_t deniedcount = 0;

  for(;;)
  {
    if (Serial.available())
    {
      uint8_t input = Serial.read();
      if (input == '\n')
      {
        SetLEDState(LEDState_Busy);
        Serial.println("ready");

        char    cmdbuf[CMD_BUFSIZE] = {};
        uint8_t cmdbuffill;
        if (ReadCMD(cmdbuf, &cmdbuffill))
          ParseCMD(cmdbuf, cmdbuffill);
      }
    }

    SetLEDState(LEDState_Reading);

    ds.reset_search();
    if (ds.search(addr) && OneWire::crc8(addr, 7) == addr[7])
    {
      Serial.print("DEBUG: Found iButton with address: ");
      for (uint8_t i = 0; i < sizeof(addr); i++)
        Serialprintf("%02x", addr[i]);
      Serial.print('\n');

      if (AuthenticateButton(addr))
      {
        SetLEDState(LEDState_Authorized);
        Serial.print("iButton authenticated\n");
        ToggleLock();
        deniedcount = 0;

        if(g_lockopen == true){
          StateSolenoid = true;
          SolenoidStartTime = millis();
          Serial.print("Solenoid activated\n");
          digitalWrite(PIN_SOLENOID, HIGH);
          stepper.move(MOTOR_STEPS*(RPM/60)*10);
        }

      }
      else
      {
        deniedcount++;
        if (deniedcount == 3)
        {
          Serial.print("iButton not authenticated\n");
          SetLEDState(LEDState_Busy);
          //disabled because sounding the horn resets the arduino
          //digitalWrite(PIN_HORN, HIGH);
          //DelayLEDs(500);
          //digitalWrite(PIN_HORN, LOW);
          deniedcount = 0;
        }
      }
    }
    else
    {
      deniedcount = 0;
    }

    ProcessLEDs();

    digitalWrite(PIN_LEDSOLENOID, HIGH);
    digitalWrite(PIN_LEDHORN, HIGH);
    if (digitalRead(INPUT_SOLENOID) == LOW) {
      if(StateSolenoid == false){
        StateSolenoid = true;
        SolenoidStartTime = millis();
        Serial.print("Solenoid activated\n");
        digitalWrite(PIN_SOLENOID, HIGH);
        stepper.move(MOTOR_STEPS*(RPM/60)*10);
      }
    }
    if(StateSolenoid == true && ((millis() - SolenoidStartTime) > (10*1000)) ){
      digitalWrite(PIN_SOLENOID, LOW);
      StateSolenoid = false;
    }
    if (digitalRead(INPUT_HORN) == LOW) {
      if(StateHorn == false){
        StateHorn = true;
        Serial.print("Horn activated\n");
        digitalWrite(PIN_HORN, HIGH);
      }
    }else{
      StateHorn = false;
      digitalWrite(PIN_HORN, LOW);
    }


  }
}

