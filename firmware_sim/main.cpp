
#include <iostream>
#include <memory>
#include <unistd.h>
#include <time.h>
#include <math.h>   // for adding variation to simulated temperature.

#include "temperature_interface.h"
#include "data_mover.h"
#include "sample_sound.h"
#include "hardware_interface.h"
#include "action_manager.h"
#include "time_interface.h"
#include "time_manager.h"

std::shared_ptr<ActionManager> action_manager;

class TimeInterfaceSim: public TimeInterface {
  public:
 
  unsigned int secondsSince1970() override {
    return time(nullptr);
  } 

  unsigned int msSinceDeviceStart() override {
    timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t );
    const unsigned int msFromS  = t.tv_sec * 1000;
    const unsigned int msFromNs = t.tv_nsec / 1000000;
    return msFromS + msFromNs; 
  }
};

class NetConnectionSim: public NetConnection {
  bool getString( std::string& string ) {
    string = "";
    return true;
  }
  operator bool(void) {
    return true;
  }
  void reset(void )
  {
  }
  std::streamsize write( const char_type* s, std::streamsize n )
  {
    for ( std::streamsize i = 0; i < n; ++i ) {
      std::cout << s[i];
    }
    return n;
  }
  void flush()
  {
  }
};

class NetInterfaceSim: public NetInterface {
  public:

  struct category: virtual beefocus_tag {};
  using char_type = char;

  NetInterfaceSim( std::shared_ptr<DebugInterface> debugLog )
  {
    (*debugLog) << "Simulator Net Interface Init\n";
  }
  bool getString( std::string& input ) override
  {
    fd_set readfds;
    FD_ZERO(&readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    FD_SET(STDIN_FILENO, &readfds );
    if ( select(1, &readfds, nullptr, nullptr, &timeout ))
    {
      std::cin >> input;
      return true;
    }
    input = "";
    return false;
  }
  std::streamsize write( const char_type* s, std::streamsize n ) override
  {
    for ( std::streamsize i = 0; i < n; ++i ) {
      std::cout << s[i];
    }
    return n;
  }
  void flush() override
  {
  }
  const char* debugName() override { return "NetInterfaceSim"; }
  unsigned int loop() override {
    return 5000000;
  }
  std::unique_ptr<NetConnection> connect( const std::string& location, unsigned int port ) override
  {
    return std::unique_ptr<NetConnection>(new NetConnectionSim());  
  }
};

class HWISim: public HWI
{
  public: 

  void PinMode( Pin pin, PinIOMode mode ) override
  {
    std::cout << "PM (" << HWI::pinNames.at(pin) << ") = " << HWI::pinIOModeNames.at(mode) << "\n";
  }
  void DigitalWrite( Pin pin, PinState state ) override
  {
    const std::string name = HWI::pinNames.at(pin);
    std::cout << "DW (" << HWI::pinNames.at(pin) 
              << ") = " << HWI::pinStateNames.at( state ) 
              << "\n";
  }
  PinState DigitalRead( Pin pin ) override
  {
    std::cout << "DR " << HWI::pinNames.at(pin) << " returning HOME_INACTIVE";
    return HWI::PinState::DUMMY_INACTIVE;
  }
  unsigned AnalogRead( Pin pin ) override
  {
    static unsigned int count = 0;
    int count_pos = ((count / 2 ) & 0xfff )/256;   // Range 0 - 0xff
    int count_amp = (count & 1) ? count_pos : -count_pos ;
    count++;

    return 200 + count_amp;
  }
};

class TempSim: public TempInterface {
  public:
  float readTemperature() override
  {
    static float angle = 0.0f;
    angle += .5;
    return 20.0 + sin( angle );
  }
  float readHumidity() override
  {
    return 50.0f;
  }
};

class DebugInterfaceSim: public DebugInterface
{
  struct category: virtual beefocus_tag {};
  using char_type = char;

  std::streamsize write( const char_type* s, std::streamsize n ) override
  {
    // Ignore for now.
    return n;
  }
  void disable() override 
  {
    // Can't disable what we're ignoring.
  }
};

unsigned int loop() {
  return action_manager->loop();
}

void setup() {
  auto debug     = std::make_shared<DebugInterfaceSim>();
  auto wifi      = std::make_shared<NetInterfaceSim>( debug );
  auto hardware  = std::make_shared<HWISim>();
  auto timeSim   = std::make_shared<TimeInterfaceSim>();
  auto time      = std::make_shared<TimeManager>( timeSim );
  auto temp      = std::make_shared<TempSim>();
  //auto sound     = std::make_shared<FS::SSound>( wifi, hardware, debug, time );
  auto datamover = std::make_shared<DataMover>( "sim", temp, wifi );

  action_manager = std::make_shared<ActionManager>( wifi, hardware, debug );
  //action_manager->addAction( sound );
  action_manager->addAction( time );
  action_manager->addAction( datamover );
  action_manager->addAction( wifi );
}

int main(int argc, char* argv[])
{
  setup();
  for ( ;; ) 
  {
    unsigned int delay = loop();
    usleep( delay );
  }
}

