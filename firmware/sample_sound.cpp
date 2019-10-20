
#include <iterator>
#include <vector>
#include <string>
#include <memory>
#include "command_parser.h"
#include "wifi_debug_ostream.h"
#include "sample_sound.h"

using namespace FS;

/////////////////////////////////////////////////////////////////////////
//
// Public Interfaces
//
/////////////////////////////////////////////////////////////////////////

SSound::SSound(
    std::unique_ptr<NetInterface> netArg,
    std::unique_ptr<HWI> hardwareArg,
    std::unique_ptr<DebugInterface> debugArg
)
{
  std::swap( net, netArg );
  std::swap( hardware, hardwareArg );
  std::swap( debugLog, debugArg );
  
  DebugInterface& dlog = *debugLog;
  dlog << "Bringing up net interface\n";
  
  // Bring up the interface to the controlling computer

  net->setup( dlog );
  WifiDebugOstream log( debugLog.get(), net.get() );

  //hardware->PinMode(HWI::Pin::STEP,       HWI::PinIOMode::M_OUTPUT );  
 
  //hardware->DigitalWrite( HWI::Pin::DIR, HWI::PinState::DIR_FORWARD); 

  log << "SSound is up\n";
}

unsigned int SSound::loop()
{
  ptrToMember function = stateImpl.at( stateStack.topState() );
  const unsigned uSecToNextCall = (this->*function)();
  uSecRemainder += uSecToNextCall;
  time += uSecRemainder / 1000;
  uSecRemainder = uSecRemainder % 1000;
  net->flush();
  return uSecToNextCall;
}

/////////////////////////////////////////////////////////////////////////
//
// Private Interfaces
//
// Section Overview:
// 
// 1. Constant Data
// 2. Methods that interpret input from the network
// 3. Methods that process the commands over time
// 4. Utility Methods
// 
/////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////
//
// Section 1.  Constant Data
//
/////////////////////////////////////////////////////////////////////////

// What does the focuser execute if it's in a particular state?
const std::unordered_map<State,unsigned int (SSound::*)( void ),EnumHash>
  SSound::stateImpl =
{
  { State::ACCEPT_COMMANDS,           &SSound::stateAcceptCommands },
  { State::SAMPLE_1SEC_SOUNDS_COL,    &SSound::stateSample1SecCollector},
  { State::SAMPLE_1SEC_SOUNDS,        &SSound::stateSample1Sec},
  { State::SAMPLE_1HR,                &SSound::stateSample1Hr},
  { State::SAMPLE_1HR_COL,            &SSound::stateSample1HrCollector},
  { State::DO_PAUSE,                  &SSound::stateDoingPause},
  { State::ERROR_STATE,               &SSound::stateError }
};

// Bind State Enums to Human Readable Debug Names
const StateToString FS::stateNames =
{
  { State::ACCEPT_COMMANDS,               "ACCEPTING_COMMANDS" },
  { State::SAMPLE_1SEC_SOUNDS_COL,        "Collecting Samples" },
  { State::SAMPLE_1SEC_SOUNDS,            "Collect 1 Sec of Samples"},
  { State::SAMPLE_1HR,                    "1Hr Sound Histogram" },
  { State::SAMPLE_1HR_COL,                "Collecting 1Hr Histogram Samples"},
  { State::DO_PAUSE,                      "Collect Sound Histogram Idle"},
  { State::ERROR_STATE,                   "ERROR ERROR ERROR"  },
};

// Implementation of the commands that the SSound Supports 
const std::unordered_map<CommandParser::Command,
  void (SSound::*)( CommandParser::CommandPacket),EnumHash> 
  SSound::commandImpl = 
{
  { CommandParser::Command::Abort,      &SSound::doAbort },
  { CommandParser::Command::Status,     &SSound::doStatus },
  { CommandParser::Command::NoCommand,  &SSound::doError },
};

// Can a command be interrupted/aborted?
const CommandToBool FS::doesCommandInterrupt= 
{
  { CommandParser::Command::Abort,         true   },
  { CommandParser::Command::Status,        false  },
  { CommandParser::Command::NoCommand,     false  },
};

/////////////////////////////////////////////////////////////////////////
//
// Section 2. Methods that interpret input from the network
//
/////////////////////////////////////////////////////////////////////////

// Entry point for all commands
void SSound::processCommand( CommandParser::CommandPacket cp )
{
  if ( doesCommandInterrupt.at( cp.command ))
  {
    timeLastInterruptingCommandOccured = time;
  }
  auto function = commandImpl.at( cp.command );
  (this->*function)( cp );
}

void SSound::doAbort( CommandParser::CommandPacket cp )
{
  (void) cp;
  // Do nothing - command triggers a state interrupt.
}

void SSound::doStatus( CommandParser::CommandPacket cp )
{
  (void) cp;
  DebugInterface& log = *debugLog;
  log << "Processing status request\n";
  *net << "Status :\n";
  histogram_t::array_t histoout;
  samples.get_histogram( histoout );
  int count=0;
  *net << "min 1sec sample " << min_1sec_sample << "\n";
  *net << "max 1sec sample " << max_1sec_sample << "\n";
  for ( auto i : histoout ) {
    const char *extra_padding = ( count < 10 ) ? " " : "";
    *net << count << extra_padding << " -> ";
    for ( int c = 0; c < i; ++c ) {
      *net << "x";
    }
    *net << "\n";
    count++;
  } 
}

void SSound::doError( CommandParser::CommandPacket cp )
{
  (void) cp;
  stateStack.push( State::ERROR_STATE, __LINE__ );   
}

/////////////////////////////////////////////////////////////////////////
//
// Section 3. Methods that process the commands over time
//
/////////////////////////////////////////////////////////////////////////


unsigned int SSound::stateAcceptCommands()
{
  DebugInterface& log = *debugLog;
  auto cp = CommandParser::checkForCommands( log, *net );

  if ( cp.command != CommandParser::Command::NoCommand )
  {
    processCommand( cp );
    return 0;
  }

  stateStack.push( State::SAMPLE_1HR, 0 );

  return 1000*1000;
}

unsigned int SSound::stateSample1SecCollector()
{
  const unsigned endTime = (unsigned) stateStack.topArg().getInt();
  if ( endTime < time ) {
    stateStack.pop();
    return 0;
  }
  unsigned curSound = hardware->AnalogRead( HWI::Pin::MICROPHONE );
  min_1sec_sample = std::min( curSound, min_1sec_sample );
  max_1sec_sample = std::max( curSound, min_1sec_sample );
  return 1000;
}

unsigned int SSound::stateSample1Sec()
{
  unsigned curSound = hardware->AnalogRead( HWI::Pin::MICROPHONE );
  min_1sec_sample = curSound;
  max_1sec_sample = curSound;
  stateStack.pop();
  stateStack.push( State::SAMPLE_1SEC_SOUNDS_COL, time + 1000 );
  return 0;
}

unsigned int SSound::stateSample1HrCollector()
{
  // We pushed a 1 second sample on the stack when we started, so there's
  // guaranteed data that can be read.
  samples.insert( max_1sec_sample - min_1sec_sample );

  // Are we done?
  const unsigned endTime = (unsigned) stateStack.topArg().getInt();
  if ( endTime < time ) {
    stateStack.pop();
    return 0;
  }
  stateStack.push( State::SAMPLE_1SEC_SOUNDS, 0);
  stateStack.push( State::DO_PAUSE, time + 1000 * 3 );
  return 0;
}

unsigned int SSound::stateDoingPause()
{
  const unsigned endTime = (unsigned) stateStack.topArg().getInt();
  if ( endTime < time ) {
    stateStack.pop();
    return 0;
  }

  DebugInterface& debug= *debugLog;
  auto cp = CommandParser::checkForCommands( debug, *net );
  if ( cp.command != CommandParser::Command::NoCommand )
  {
    processCommand( cp );
    return 0;
  }

  return 1000*1000;
}

unsigned int SSound::stateSample1Hr()
{
  samples.reset();
  stateStack.push( State::SAMPLE_1HR_COL, time + 1000 * 60 * 60 * 24 );
  stateStack.push( State::SAMPLE_1SEC_SOUNDS, time + 1000 * 60 * 60 );
  return 0;
}

unsigned int SSound::stateError()
{
  WifiDebugOstream log( debugLog.get(), net.get() );
  log << "hep hep hep error error error\n";
  return 10*1000*1000; // 10 sec pause 
}


