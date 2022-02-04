
struct HealthzuillaScaleSettings {
  // This is for mere detection if they are your settings
  char version[4];
  bool useStaticIp;
  char ip[16];
  char gateway[16];
  char subnetMask[16];
  float calibrationFactor;
  long zeroFactor;
  float calibrationWeight;
  uint16_t powerOffTimerSec;
  uint16_t idlePowerOffTimerSec;
  double voltageCalibrationFactor;
};

struct FoodInfo
{
  char foodId[32];
  unsigned short calories;
  char name[50];
};
