/**
  ******************************************************************************
  * @file    epd.h
  * @brief   7.3 inch E6 e-Paper dispatch entry
  ******************************************************************************
  */
#ifndef EPD_H
#define EPD_H

#include "DEV_Config.h"
#include "epd7in3.h"

void EPD_initSPI()
{
    pinMode(EPD_BUSY_PIN, INPUT);
    pinMode(EPD_RST_PIN, OUTPUT);
    pinMode(EPD_DC_PIN, OUTPUT);
    pinMode(EPD_SCK_PIN, OUTPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_CS_PIN, OUTPUT);

    digitalWrite(EPD_CS_PIN, HIGH);
    digitalWrite(EPD_SCK_PIN, LOW);
}

int EPD_dispIndex;
bool (*EPD_dispLoad)();

struct EPD_dispInfo
{
    int (*init)();
    bool (*load)();
    void (*show)();
    const char *title;
};

EPD_dispInfo EPD_dispMass[] =
{
    { EPD_7in3E_init, EPD_load_7in3E_from_buff, EPD_7in3E_Show, "7.3 inch E6" },
};

void EPD_dispInit()
{
    EPD_dispMass[EPD_dispIndex].init();
    EPD_dispLoad = EPD_dispMass[EPD_dispIndex].load;
}

#endif
