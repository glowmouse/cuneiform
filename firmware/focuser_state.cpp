
#include <iterator>
#include <vector>
#include <string>
#include <memory>
#include "command_parser.h"
#include "wifi_debug_ostream.h"
#include "focuser_state.h"

using namespace FS;

//
// Implementation of the Focuser class
//
// As described in focuser_state.h, the Focuser Class has two main jobs:
//
// 1. It accepts new commands from a network interface
// 2. Over time, it manipulates a hardware interface to implement the commands
//
// At construction time the Focuser is provided three interfaces - an
// interface to the network (i.e., a Wifi Connection), an interface to the 
// the hardware (i.e., the pins in a Micro-Controller) an interface for
// debug logging, and the focuser's hardware parameters
//
// Once the focuser is initialized, the loop function is used to real time
// updates.  The loop function returns a minimum time that the caller should
// wait before calling loop again, in microseconds.
//
// Private member functions prefaced with 'do' are command processors.  They
// take input for a command that came from the focuser's network interface
// and adjust the Focuser class's state so it can process that input. For
// example, the doHome method pushes a State::STOP_AT_HOME command onto the
// focuser's state stack.
//
// When loop is called, the Focuser class processess the top command on its
// state stack.  The methods that do this processing are prefaced with 
// 'state'.  For example, stateStopAtHome checks to see if the hardware
// interface's home pin is active.  If it is, it pops it's current state
// (State::STOP_AT_HOME' from the focuser's state stack and considers the
// operating finished.  If it isn't, it pushes commands onto the focuser's
// state stack that will result in the focuser rewinding one step.
//

/////////////////////////////////////////////////////////////////////////
//
// Public Interfaces
//
/////////////////////////////////////////////////////////////////////////

Focuser::Focuser(
    std::unique_ptr<NetInterface> netArg,
    std::unique_ptr<HWI> hardwareArg,
    std::unique_ptr<DebugInterface> debugArg,
    const BuildParams params
) : buildParams{ params }
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

  log << "Focuser is up\n";
}

unsigned int Focuser::loop()
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
const std::unordered_map<State,unsigned int (Focuser::*)( void ),EnumHash>
  Focuser::stateImpl =
{
  { State::ACCEPT_COMMANDS,           &Focuser::stateAcceptCommands },
  { State::ERROR_STATE,               &Focuser::stateError }
};

// Bind State Enums to Human Readable Debug Names
const StateToString FS::stateNames =
{
  { State::ACCEPT_COMMANDS,               "ACCEPTING_COMMANDS" },
  { State::ERROR_STATE,                   "ERROR ERROR ERROR"  },
};

// Implementation of the commands that the Focuser Supports 
const std::unordered_map<CommandParser::Command,
  void (Focuser::*)( CommandParser::CommandPacket),EnumHash> 
  Focuser::commandImpl = 
{
  { CommandParser::Command::Abort,      &Focuser::doAbort },
  { CommandParser::Command::PStatus,    &Focuser::doPStatus },
  { CommandParser::Command::MStatus,    &Focuser::doMStatus },
  { CommandParser::Command::SStatus,    &Focuser::doSStatus },
  { CommandParser::Command::Firmware,   &Focuser::doFirmware},
  { CommandParser::Command::Caps,       &Focuser::doCaps},
  { CommandParser::Command::DebugOff,   &Focuser::doDebugOff},
  { CommandParser::Command::NoCommand,  &Focuser::doError },
};

// Can a command be interrupted/aborted?
const CommandToBool FS::doesCommandInterrupt= 
{
  { CommandParser::Command::Abort,         true   },
  { CommandParser::Command::PStatus,       false  },
  { CommandParser::Command::MStatus,       false  },
  { CommandParser::Command::SStatus,       false  },
  { CommandParser::Command::Firmware,      false  },
  { CommandParser::Command::Caps,          false  },
  { CommandParser::Command::DebugOff,      false  },
  { CommandParser::Command::NoCommand,     false  },
};

// Builds currently supported by BeeFocus
BuildParams::BuildParamMap BuildParams::builds = {
  {
    Build::LOW_POWER_HYPERSTAR_FOCUSER,
    {
      TimingParams { 
        100,        // Check for new commands every 100ms
        100,        // Take 100 steps before checking for interrupts
        5*60*1000,  // Go to sleep after 5 minutes of inactivity
        1000,       // Check for new input in sleep mode every second
        1000,       // Take 1 second to power up the focuser motor on awaken
        1000        // Wait 1000 microseconds between steps       
      },
      true,         // Focuser can use a home switch to synch
      50000         // End of the line for my focuser
    }
  },
  {
    Build::LOW_POWER_HYPERSTAR_FOCUSER_MICROSTEP,
    {
      TimingParams { 
        100,        // Check for new commands every 100ms
        1000,       // Take 1000 steps before checking for interrupts
        5*60*1000,  // Go to sleep after 5 minutes of inactivity
        1000,       // Check for new input in sleep mode every second
        1000,       // Take 1 second to power up the focuser motor on awaken
        31          // Wait 31 microseconds between steps       
      },
      true,         // Focuser can use a home switch to synch
      500000        // End of the line for my focuser
    }
  },
  { Build::UNIT_TEST_BUILD_HYPERSTAR, 
    {
      TimingParams { 
        10,         // Check for new commands every 10ms
        2,          // Take 2 steps before checking for interrupts
        1000,       // Go to sleep after 1 second of inactivity
        500,        // Check for new input in sleep mode every 500ms
        200,        // Allow 200ms to power on the motor
        1000        // Wait 1000 microseconds between steps       
      },
      true,         // Focuser can use a home switch to synch
      35000         
    }
  },
  {
    Build::TRADITIONAL_FOCUSER,
    {
      TimingParams { 
        100,        // Check for new commands every 100ms
        50,         // Take 50 steps before checking for interrupts
        10*24*60*1000,  // Go to sleep after 10 days of inactivity
        1000,       // Check for new input in sleep mode every second
        1000,       // Take 1 second to power up the focuser motor on awaken
        1000        // Wait 1000 microseconds between steps       
      },
      false,        // Focuser cannot use a home switch to synch
      5000          // Mostly a place holder
    }
  },
  { Build::UNIT_TEST_TRADITIONAL_FOCUSER, 
    {
      TimingParams { 
        10,         // Check for new commands every 10ms
        2,          // Take 2 steps before checking for interrupts
        1000,       // Go to sleep after 1 second of inactivity
        500,        // Check for new input in sleep mode every 500ms
        200,        // Allow 200ms to power on the motor
        1000        // Wait 1000 microseconds between steps       
      },
      false,        // Focuser cannot use a home switch to synch
      5000          // Mostly a place holder
    }
  },
};

/////////////////////////////////////////////////////////////////////////
//
// Section 2. Methods that interpret input from the network
//
/////////////////////////////////////////////////////////////////////////

// Entry point for all commands
void Focuser::processCommand( CommandParser::CommandPacket cp )
{
  if ( doesCommandInterrupt.at( cp.command ))
  {
    timeLastInterruptingCommandOccured = time;
  }
  auto function = commandImpl.at( cp.command );
  (this->*function)( cp );
}

void Focuser::doAbort( CommandParser::CommandPacket cp )
{
  (void) cp;
  // Do nothing - command triggers a state interrupt.
}

void Focuser::doPStatus( CommandParser::CommandPacket cp )
{
  (void) cp;
  DebugInterface& log = *debugLog;
  log << "Processing pstatus request\n";
  *net << "Position: " << focuserPosition << "\n";
}

void Focuser::doMStatus( CommandParser::CommandPacket cp )
{
  (void) cp;
  DebugInterface& log = *debugLog;

  log << "Processing mstatus request\n";
  *net << "State: " << stateNames.at(stateStack.topState()) << 
                " " << stateStack.topArg() << "\n";
}

void Focuser::doSStatus( CommandParser::CommandPacket cp )
{
  (void) cp;
  DebugInterface& log = *debugLog;

  log << "Processing sstatus request\n";
  *net << "Synched: " << (isSynched ? "YES" : "NO" ) << "\n";
}

void Focuser::doFirmware( CommandParser::CommandPacket cp )
{
  (void) cp;
  DebugInterface& log = *debugLog;

  log << "Processing firmware request\n";
  *net << "Firmware: 1.0\n";
}

void Focuser::doCaps( CommandParser::CommandPacket cp )
{
  (void) cp;
  DebugInterface& log = *debugLog;

  log << "Processing capabilities request\n";
  *net << "MaxPos: " << buildParams.maxAbsPos << "\n";
  *net << "CanHome: " << (buildParams.focuserHasHome ? "YES\n" : "NO\n" );
}

void Focuser::doDebugOff( CommandParser::CommandPacket cp )
{
  (void) cp;
  DebugInterface& log = *debugLog;

  log << "Disabling low level debug output";
  log.disable();
}

void Focuser::doError( CommandParser::CommandPacket cp )
{
  (void) cp;
  stateStack.push( State::ERROR_STATE, __LINE__ );   
}

/////////////////////////////////////////////////////////////////////////
//
// Section 3. Methods that process the commands over time
//
/////////////////////////////////////////////////////////////////////////


unsigned int Focuser::stateAcceptCommands()
{
  DebugInterface& log = *debugLog;
  auto cp = CommandParser::checkForCommands( log, *net );

  if ( cp.command != CommandParser::Command::NoCommand )
  {
    processCommand( cp );
    return 0;
  }
  const unsigned int timeSinceLastInterrupt = 
      time - timeLastInterruptingCommandOccured;

  const int timeBetweenChecks = buildParams.timingParams.getEpochBetweenCommandChecks();
  const int mSecToNextEpoch = timeBetweenChecks - ( time % timeBetweenChecks );

  return mSecToNextEpoch * 1000;
}

unsigned int Focuser::stateError()
{
  WifiDebugOstream log( debugLog.get(), net.get() );
  log << "hep hep hep error error error\n";
  return 10*1000*1000; // 10 sec pause 
}


