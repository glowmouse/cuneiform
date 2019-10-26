
#include <memory>
#include "sample_sound.h"
#include "net_esp8266.h"
#include "hardware_esp8266.h"
#include "debug_esp8266.h"
#include "action_manager.h"
#include "time_esp8266.h"
#include "time_manager.h"

std::shared_ptr<ActionManager> action_manager;

void loop() {
  unsigned int pause = action_manager->loop();
  if ( pause != 0 )
  {
    int ms = pause / 1000;
    int usRemainder = pause % 1000;
    delay( ms );
    delayMicroseconds( usRemainder );
  }
}

void setup() {
  auto wifi      = std::make_shared<WifiInterfaceEthernet>();
  auto hardware  = std::make_shared<HardwareESP8266>();
  auto debug     = std::make_shared<DebugESP8266>();
  auto timeNNTP  = std::make_shared<TimeESP8266>( debug );
  auto time      = std::make_shared<TimeManager>( timeNNTP );
  auto sound     = std::make_shared<FS::SSound>( wifi, hardware, debug, time );

  action_manager = std::make_shared<ActionManager>( wifi, hardware, debug );
  action_manager->addAction( sound );
  action_manager->addAction( time );
}
