#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

class DisplayHelper
{
    protected:
        Adafruit_PCD8544* display;

    public:
        DisplayHelper(Adafruit_PCD8544* display): display(display){};
        void renderSignalStrength(uint32_t rssi);
        void renderWiFiDisconnected();
        void renderBatteryLevel(uint8_t level, uint8_t x = 0, uint8_t y = 2, uint8_t width = 40, uint8_t height = 10);
        void renderAll();
};
