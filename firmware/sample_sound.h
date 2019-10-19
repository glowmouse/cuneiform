#ifndef __FOCUSER_STATE_H__
#define __FOCUSER_STATE_H__

#include <vector>
#include <memory>
#include <string>
#include <assert.h>
#include <unordered_map>
#include "net_interface.h"
#include "hardware_interface.h"
#include "command_parser.h"

#ifdef GTEST_FOUND
#include <gtest/gtest_prod.h>
#endif

///
/// @brief SSound Namespace
/// 
namespace FS {

/// @brief SSound's State Enum
///
/// @see FS Namespace for high level description.
///
enum class State 
{
  START_OF_STATES = 0,        ///< Start of States
  ACCEPT_COMMANDS = 0,        ///< Accepting commands from the net interface
  SAMPLE_1SEC_SOUNDS,         ///< Get the max sound over 1 second
  SAMPLE_1SEC_SOUNDS_COL,     ///< Collector state for SAMPLE_1SEC_SOUNDS
  ERROR_STATE,                ///< Error Errror Error
  END_OF_STATES               ///< End of States
};

/// @brief What direction is the focuser going?
enum class Dir {
  FORWARD,    ///< Go Forward
  REVERSE     ///< Go Backward
};

class StateArg
{
  public: 

  enum class Type {
    NONE,
    INT,
    DIR
  };

  StateArg() : type{ Type::NONE}  {}
  StateArg( int i ) : type{Type::INT}, intArg{ i } {}
  StateArg( Dir d ) : type{Type::DIR}, dirArg{ d } {}
  Type getType() { return type; }
  int getInt() { assert( type==Type::INT ); return intArg; }
  Dir getDir() { assert( type==Type::DIR ); return dirArg; }

  private:

  Type type;

  union {
    int intArg;
    Dir dirArg;
  };
};

#ifdef gone
class TimingParams
{
  public:

  TimingParams( 
    int msEpochBetweenCommandChecksRHS    = 100,        // 100 ms
    int maxStepsBetweenChecksRHS          = 50,
    unsigned msInactivityToSleepRHS       = 5*60*1000,  // 5 minutes
    int msEpochForSleepCommandChecksRHS   = 1*1000,     // 1 seconds
    int msToPowerStepperRHS               = 1*1000,     // 1 second
    unsigned microSecondStepPauseRHS      = 1000        // 1 ms
  ) :
    msEpochBetweenCommandChecks{ msEpochBetweenCommandChecksRHS },
    maxStepsBetweenChecks{ maxStepsBetweenChecksRHS },
    msInactivityToSleep{ msInactivityToSleepRHS },
    msEpochForSleepCommandChecks{ msEpochForSleepCommandChecksRHS },
    msToPowerStepper{ msToPowerStepperRHS},
    microSecondStepPause{ microSecondStepPauseRHS }
  {
  }

  int getEpochBetweenCommandChecks() const 
  { 
    return msEpochBetweenCommandChecks; 
  }
  int getMaxStepsBetweenChecks() const 
  { 
    return maxStepsBetweenChecks; 
  }
  unsigned getInactivityToSleep() const 
  { 
    return msInactivityToSleep; 
  }
  int getEpochForSleepCommandChecks() const 
  { 
    return msEpochForSleepCommandChecks; 
  }
  int getTimeToPowerStepper() const 
  { 
    return msToPowerStepper;
  }
  int getMicroSecondStepPause() const 
  { 
    return microSecondStepPause;
  }

  private:
  int msEpochBetweenCommandChecks;
  int maxStepsBetweenChecks;
  unsigned msInactivityToSleep;
  int msEpochForSleepCommandChecks;
  int msToPowerStepper;
  unsigned microSecondStepPause;
};
#endif

enum class Build
{
  LOW_POWER_HYPERSTAR_FOCUSER,
  LOW_POWER_HYPERSTAR_FOCUSER_MICROSTEP,
  TRADITIONAL_FOCUSER,
  UNIT_TEST_BUILD_HYPERSTAR,
  UNIT_TEST_TRADITIONAL_FOCUSER
};

///
/// @brief Stack of FS:States.
///
/// Invariants:
///
/// - In normal operation, the stack's bottom is always an ACCEPT_COMMANDS state
/// - After construction, the stack can never be empty
/// - If a pop operation leaves the stack empty an ERROR_STATE is pushed
/// 
class StateStack {
  public:

  StateStack()
  {
    push( State::ACCEPT_COMMANDS, StateArg() );
  }

  /// @brief Reset the stack to the newly initialized state.
  void reset( void )
  {
    while ( stack.size() > 1 ) pop();
  }

  /// @brief Get the top state.
  State topState( void )
  {
    return stack.back().state;
  }

  /// @brief Get the top state's argumment.
  StateArg topArg( void )
  {
    return stack.back().arg;
  }

  /// @brief Set the top state's argumment.
  void topArgSet( StateArg newVal )
  {
    stack.back().arg = newVal;
  }

  /// @brief Pop the top entry on the stack.
  void pop( void )
  {
    stack.pop_back();
    if ( stack.size() > 10 ) 
    {
      push( State::ERROR_STATE, StateArg(__LINE__) ); 
    }
    if ( stack.empty() ) 
    {
      // bug, should never happen.
      push( State::ERROR_STATE, StateArg(__LINE__) ); 
    }
  }

  /// @brief Push a new entry onto the stack
  void push( State newState, StateArg newArg = StateArg() )
  {
    stack.push_back( { newState , newArg } );
  }  
  
  private:

  typedef struct 
  {
    State state;   
    StateArg arg; 
  } CommandPacket;

  std::vector< CommandPacket > stack;
};

/// @brief Main SSound Class
///
/// The SSound class has two main jobs:
///
/// 1. It accepts new commands from a network interface
/// 2. Over time, it manipulates a hardware interface to implement the commands
///
/// At construction time the SSound is provided three interfaces - an
/// interface to the network (i.e., a Wifi Connection), an interface to the 
/// the hardware (i.e., the pins in a Micro-Controller) an interface for
/// debug logging, and the focuser's hardware parameters
///
/// Once the focuser is initialized, the loop function is used to real time
/// updates.  The loop function returns a minimum time that the caller should
/// wait before calling loop again, in microseconds.
///
/// the main event loop could look something like the following:
///
/// SSound( std::move(net), std::move(hardware), std::move(debug), params );
/// for ( ;; ) {
///   unsigned int delay = SSound.loop();
///   delayMicroseconds( delay );   
/// }
/// 
class SSound 
{
  public:
 
  /// @brief SSound State Constructor
  ///
  /// @param[in] netArg       - Interface to the network
  /// @param[in] hardwareArg  - Interface to the Hardware
  /// @param[in] debugArg     - Interface to the debug logger.
  /// @param[in] params       - Hardware Parameters 
  ///
  SSound( 
		std::unique_ptr<NetInterface> netArg,
		std::unique_ptr<HWI> hardwareArg,
		std::unique_ptr<DebugInterface> debugArg
	);

  ///
  /// @brief Update the SSound's State
  ///
  /// @return The amount of time the caller should wait (in microseconds)
  ///         before calling loop again.
  ///
  unsigned int loop();

  private:

#ifdef GTEST_FOUND
  // So we can unit test the consistency of the class's constant - static 
  // data without exposing it to everybody
  FRIEND_TEST(FOCUSER_STATE, allStatesHaveImplementations);
  FRIEND_TEST(FOCUSER_STATE, allCommandsHaveImplementations);
#endif

  static const std::unordered_map<CommandParser::Command,
    void (SSound::*)( CommandParser::CommandPacket),EnumHash> 
    commandImpl;

  using ptrToMember = unsigned int ( SSound::*) ( void );
  static const std::unordered_map< State, ptrToMember, EnumHash > stateImpl;

  /// @brief Deleted copy constructor
  SSound( const SSound& other ) = delete;
  /// @brief Deleted default constructor
  SSound() = delete;
  /// @brief Deleted assignment operator
  SSound& operator=( const SSound& ) = delete;
  
  StateStack stateStack;

  void processCommand( CommandParser::CommandPacket cp );

  /// @brief Wait for commands from the network interface
  unsigned int stateAcceptCommands( void ); 
  /// @brief Collect a sound sample. Exit when the time is past the state arg
  unsigned int stateSample1SecCollector( void ); 
  /// @brief Sample sound for 1 second, computing the maximum volume
  unsigned int stateSample1Sec( void ); 
  /// @brief If we land in this state, complain a lot.
  unsigned int stateError( void );

  void doAbort( CommandParser::CommandPacket );
  void doStatus( CommandParser::CommandPacket );
  void doError( CommandParser::CommandPacket );

  std::unique_ptr<NetInterface> net;
  std::unique_ptr<HWI> hardware;
  std::unique_ptr<DebugInterface> debugLog;
  
  unsigned min_1sec_sample;
  unsigned max_1sec_sample;

  /// @brief SSound uptime in MS
  unsigned int time;

  /// @brief For computing time in SSound::loop
  unsigned int uSecRemainder;

  /// @brief Time the last command that could have caused an interrupt happened
  unsigned int timeLastInterruptingCommandOccured;
};

/// @brief Increment operator for State enum
///
inline State& operator++( State &s )
{
  return BeeFocus::advance< State, State::END_OF_STATES>(s);
}

/// @brief State to std::string Unordered Map
using StateToString = std::unordered_map< State, const std::string, EnumHash >;
/// @brief Command to bool Unordered Map
using CommandToBool = std::unordered_map< CommandParser::Command, bool, EnumHash >;

extern const StateToString stateNames;
///
/// @brief Does a particular incoming command interrupt the current state
///
/// Example 1.  A "Status" Command will not interrupt a move sequence
/// Example 2.  A "Home" Command will interrupt a focuser's move sequence
///
extern const CommandToBool doesCommandInterrupt;

/// @brief Output StateArg
template <class T,
  typename = my_enable_if_t<is_beefocus_sink<T>::value>>
T& operator<<( T& sink, StateArg sA )
{
  switch (sA.getType() )
  {
    case StateArg::Type::NONE:
      sink << "NoArg";
      break;
    case StateArg::Type::INT:
      sink << sA.getInt();
      break;
    default:
      assert(0);
  }
  return sink; 
}

}

#endif
