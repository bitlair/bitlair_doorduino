#include "OneWire.h"
#include "ds1961.h"

#include <stdint.h>

#define PIN_1WIRE              2
#define PIN_LEDGREEN           3
#define PIN_LEDRED             4

#define CMD_BUFSIZE            64
#define CMD_TIMEOUT            10000 //command timeout in milliseconds
#define CMD_SET_SECRET         "set_secret"
#define CMD_PING               "ping"

#define SECRETSIZE             8
#define ADDRSIZE               8

#define IBUTTON_SEARCH_TIMEOUT 60000 //timeout searching for ibutton

OneWire ds(PIN_1WIRE);
DS1961  ibutton(&ds);

void setup()
{
  Serial.begin(115200);
  Serial.println("DEBUG: Board started");
  pinMode(PIN_LEDGREEN, OUTPUT);
  pinMode(PIN_LEDRED, OUTPUT);
}

uint8_t ReadCMD(char* cmdbuf)
{
  uint32_t cmdstarttime;
  uint8_t  cmdbuffill = 0;
  for(;;)
  {
    if (Serial.available())
    {
      char input = Serial.read();
      if (input == '\n')
      {
        cmdbuf[cmdbuffill] = 0;
        return cmdbuffill;
      }
      else if (cmdbuffill < CMD_BUFSIZE - 1)
      {
        if (cmdbuffill == 0)
          cmdstarttime = millis();

        cmdbuf[cmdbuffill] = input;
        cmdbuffill++;
      }
    }
    else if (cmdbuffill > 0 && millis() - cmdstarttime >= CMD_TIMEOUT)
    {
      Serial.println("ERROR: timeout receiving command");
      return 0;
    }
  }
}

bool GetSecretFromBuf(char* cmdbuf, uint8_t cmdbuffill, uint8_t* secret)
{
  uint8_t secretpos = strlen(CMD_SET_SECRET);
  while (cmdbuf[secretpos] == ' ' && secretpos < cmdbuffill)
    secretpos++;

  if (secretpos == cmdbuffill)
  {
    Serial.println("ERROR: no secret received");
    return false;
  }
  else if (cmdbuffill - secretpos < SECRETSIZE * 2)
  {
    Serial.println("ERROR: received secret is too short");
    return false;
  }

  for (uint8_t i = 0; i < SECRETSIZE; i++)
  {
    int numread = sscanf(cmdbuf + secretpos + i * 2, "%2hhx", secret + i);
    if (numread == 0)
    {
      Serial.println("ERROR: received secret is invalid");
      return false;
    }
  }

  Serial.print("INFO: received secret ");
  for (uint8_t i = 0; i < SECRETSIZE; i++)
  {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", secret[i]);
    Serial.print(buf);
  }
  Serial.print('\n');

  return true;
}

void WriteSecretToButton(uint8_t* secret)
{
  Serial.println("INFO: searching for iButton");
  uint32_t searchstart = millis();
  digitalWrite(PIN_LEDRED, HIGH);
  do
  {
    ds.reset_search();

    uint8_t addr[ADDRSIZE];
    if (ds.search(addr) && OneWire::crc8(addr, 7) == addr[7])
    {
      Serial.print("INFO: Found iButton with address: ");
      for (uint8_t i = 0; i < sizeof(ADDRSIZE); i++)
      {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", addr[i]);
        Serial.print(buf);
      }
      Serial.print('\n');

      if (ibutton.WriteSecret(addr, secret))
      {
        digitalWrite(PIN_LEDRED, LOW);
        digitalWrite(PIN_LEDGREEN, HIGH);

        Serial.print("INFO: wrote secret ");
        for (uint8_t i = 0; i < SECRETSIZE; i++)
        {
          char buf[3];
          snprintf(buf, sizeof(buf), "%02x", secret[i]);
          Serial.print(buf);
        }
        Serial.print(" to iButton with ID ");
        for (uint8_t i = 0; i < ADDRSIZE; i++)
        {
          char buf[3];
          snprintf(buf, sizeof(buf), "%02x", addr[i]);
          Serial.print(buf);
        }
        Serial.print('\n');

        delay(2000);

        return;
      }
      else
      {
        Serial.println("Writing secret failed");
      }
    }
  }
  while (millis() - searchstart < IBUTTON_SEARCH_TIMEOUT);

  Serial.println("ERROR: timeout writing secret to iButton");
}

void WriteSecret(char* cmdbuf, uint8_t cmdbuffill)
{
  Serial.println("DEBUG: received set secret command");

  uint8_t secret[SECRETSIZE];
  if (!GetSecretFromBuf(cmdbuf, cmdbuffill, secret))
    return;

  WriteSecretToButton(secret);
}

void loop()
{
  digitalWrite(PIN_LEDGREEN, LOW);
  digitalWrite(PIN_LEDRED, LOW);

  char     cmdbuf[CMD_BUFSIZE] = {};
  uint8_t  cmdbuffill = ReadCMD(cmdbuf);

  if (cmdbuffill == 0)
    return;

  if (strncasecmp(CMD_SET_SECRET, cmdbuf, strlen(CMD_SET_SECRET)) == 0)
    WriteSecret(cmdbuf, cmdbuffill);
  else if (strncasecmp(CMD_PING, cmdbuf, strlen(CMD_PING)) == 0)
    Serial.println("pong");
  else
    Serial.println("unknown command");
}

