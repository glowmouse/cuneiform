
#include <memory>
#include "sample_sound.h"
#include "net_esp8266.h"
#include "hardware_esp8266.h"
#include "debug_esp8266.h"

std::unique_ptr<FS::SSound> soundSampler;

void loop() {
  unsigned int pause = soundSampler->loop();
  if ( pause != 0 )
  {
    int ms = pause / 1000;
    int usRemainder = pause % 1000;
    delay( ms );
    delayMicroseconds( usRemainder );
  }
}

void setup() {
  std::unique_ptr<NetInterface> wifi( new WifiInterfaceEthernet );
  std::unique_ptr<HWI> hardware( new HardwareESP8266 );
  std::unique_ptr<DebugInterface> debug( new DebugESP8266 );
  soundSampler = std::unique_ptr<FS::SSound>(
     new FS::SSound( 
        std::move(wifi), 
        std::move(hardware),
				std::move(debug) )
  );
}
