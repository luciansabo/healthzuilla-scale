#include "DisplayHelper.h"
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

void DisplayHelper::renderSignalStrength(uint32_t rssi)
{
  uint8_t percentage;
  if (rssi <= -100) {
    percentage = 0;
  } else if(rssi >= -50) {
    percentage = 100;
  } else {
    percentage = 2 * (rssi + 100);
  }

  // level is 1 - 5
  uint8_t level = round(percentage / 20.0);

  uint8_t x = 62, y = 11;
  display->drawFastHLine(x, y, 19, BLACK);

  uint8_t newX = x;
  for (uint8_t i = 0; i < level; i++) {
    display->drawLine(newX, y - 2, newX, y - 2 - ((i * 2) + 1), BLACK);
    display->drawLine(newX + 1, y - 2, newX + 1, y - 2 - ((i * 2) + 1), BLACK);
    display->drawLine(newX + 2, y - 2, newX + 2, y - 2 - ((i * 2) + 1), BLACK);
    newX += 4;
  }
}

void DisplayHelper::DisplayHelper::renderBatteryLevel(uint8_t level, uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
  display->drawRect(x, y, width, height, BLACK);
  display->fillRect(x + 2, y + 2, round((level * (width - 2)) / 100.0), height - 4, BLACK);
}

void DisplayHelper::renderWiFiDisconnected()
{
  uint8_t x = 58, y = 5;
  display->setCursor(x, y);
  display->setTextSize(1);
  display->print("WiFi"); 
  display->drawLine(x, 0, x + 26, y + 10, BLACK);
}

void DisplayHelper::renderAll()
{
    display->display();
}
