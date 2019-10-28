#include "data_mover.h"
#include "net_interface.h"
#include "temperature_interface.h"
#include "wifi_ostream.h"

unsigned int DataMover::loop() 
{
  float t = temp->readTemperature();
  int t_int = t*10.0f;
  (*net) << "temperature " << t_int << "\n";
  return 1000000;
}

