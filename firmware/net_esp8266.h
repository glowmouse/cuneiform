#ifndef __WifiInterfaceEthernet_H__
#define __WifiInterfaceEthernet_H__

#include <string>
#include <memory>
#include <ios>
#include <ESP8266WiFi.h>
#include "wifi_ostream.h"
#include "wifi_secrets.h"
#include "debug_interface.h"

class WifiOstream;

class WifiDebugOstream;

class WifiConnectionEthernet: public NetConnection {

  public:

  struct category: beefocus_tag {};
  using char_type = char;

  WifiConnectionEthernet()
  {
    reset();
  }

  ~WifiConnectionEthernet()
  {
    reset();
  }

  void reset( void ) 
  { 
    m_currentIncomingBuffer=0;
    m_incomingBuffers[0].resize(0);
    m_incomingBuffers[1].resize(0);
    if (m_connectedClient)
    {
      m_connectedClient.stop();
    }
  }

  void initConnection( WiFiServer &server );
  bool getString( std::string& string ) override;
  operator bool( void ) override {
    return m_connectedClient;
  }

  std::streamsize write( const char_type* s, std::streamsize n ) override; 
  void flush() override;

  private:

  void handleNewIncomingData();    

  int m_currentIncomingBuffer;
  std::string m_incomingBuffers[2];
  WiFiClient m_connectedClient;
  std::array< char, 1500> outgoingBuffer;
  size_t bytesInOutBuffer = 0;
  bool allOutputFlushed = true;
};

/// @brief Interface to the client
///
/// This class's one job is to provide an interface to the client.
///
class WifiInterfaceEthernet: public NetInterface {
  public:

  WifiInterfaceEthernet( std::shared_ptr<DebugInterface> debugLog );

  ~WifiInterfaceEthernet()
  {
    reset();
  }

  void reset( void );

  bool getString( std::string& string ) override;
  std::streamsize write( const char_type* s, std::streamsize n ) override;
  void flush() override;

  unsigned int loop() override;
  const char* debugName() override
  {
    return "WifiInterfaceEthernet";
  }
  std::unique_ptr<NetConnection> connect( const std::string& location, unsigned int port );
  
  private:

  void handleNewConnections();
  typedef std::array< WifiConnectionEthernet, 4 > ConnectionArray;

  // Make a CI Test to lock these defaults in?
  static constexpr const char* ssid = WifiSecrets::ssid; 
  static constexpr const char* password = WifiSecrets::password;
  static constexpr const char* hostname = WifiSecrets::hostname;
  const uint16_t tcp_port{4999};

  std::shared_ptr<DebugInterface> log;
  int m_lastSlotAllocated;
  int m_kickout;
  ConnectionArray m_connections;
  ConnectionArray::iterator m_nextToKick;

  WiFiServer m_server{tcp_port};
};

#endif

