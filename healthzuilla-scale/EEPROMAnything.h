#include <EEPROM.h>

template <class T> int EEPROM_writeAnything(int ee, const T& value, const T& oldValue)
{
    const byte* p = (const byte*)(const void*)&value;
    const byte* pOld = (const byte*)(const void*)&oldValue;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++) {
          if (*p != *pOld) {
              EEPROM.write(ee, *p);
          }
          ee++;
          *p++;
          *pOld++;
    }

    EEPROM.commit();
    return i;
}

template <class T> int EEPROM_readAnything(int ee, T& value)
{
    byte* p = (byte*)(void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          *p++ = EEPROM.read(ee++);
    return i;
}
