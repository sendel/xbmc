#pragma once
/*
 This class represents the communication layer between the Plug-in
 and the BG Process. It sends and receives events through a TCP
 socket. It receives commands in an asynchronous way. A thread
 implements the event loop: it's mainly blocked in a recv call,
 as soon as it receives an event from the BG process, it calls
 all the handlers registered for that event. The communication
 protocol it's basic: after the connection with the remote
 process, the P2PControl sends the MRL ( media resource
 locator) of the torrent to the BG, when the stream is available,
 a 'PLAY' event is sent back and the handler for that event is
 called. Since it's intended to work as a library, external code
 can use the P2P control by registering their handler functions.
*/

/*
 * For linux by SEN (C) VIOLONIX
 */

#ifndef P2PCONTROL_H_
#define P2PCONTROL_H_

#ifdef _WIN32
	#include <win32/sys/socket.h>
#endif

//#if defined(_LINUX)
#ifndef _WIN32

	#include <pthread.h>
	#include <semaphore.h>
	#include <time.h>
	#include <unistd.h>

	#include <stdio.h>
	#include <stdlib.h>
	#include <stdarg.h>
	#include <sys/socket.h>
	#include <sys/types.h>
	#include <string.h>
	#include "utils/URIUtils.h"

/*
	#include <sys/timeb.h>
	#include <sys/socket.h>

	#include <sys/wait.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <string.h>
	#include <fcntl.h>
	#include <signal.h>
	#include <errno.h>
	#include <stdlib.h>
	#include <netdb.h>
	#include <unistd.h>
*/
#endif


#include <string>
#include <map>

//void writeOnLog( const char* msg, ... );

/*************************
 * BGPConnection
**************************/
enum sock_state_t {
	S_DOWN,
	S_STARTED,
	S_CREATED,
	S_UP
};

enum ts_p2p_states_t {
	TS_P2P_IDLE = 0,
    TS_P2P_PREBUFFERING = 1,
    TS_P2P_DOWNLOADING = 2,
    TS_P2P_BUFFERING = 3,
	TS_P2P_COMPLETED = 4,
	TS_P2P_HASH_CHECKING = 5,
	TS_P2P_ERROR = 6
};

/* constants */
const int TS_ID_TORRENT_URL = 0;
const int TS_ID_DIRECT_URL = 1;
const int TS_ID_INFOHASH = 2;
const int TS_ID_PLAYER = 3;
const int TS_ID_RAW = 4;

const int IN_BUF_LEN = 512;
const int BG_PORT = 62062;
const std::string BG_ADDRESS = "127.0.0.1";
const std::string TS_PLUGIN_VERSION = "1.0.5";

class BGPConnection
{
 public:
  BGPConnection( const int port, const std::string bgAddress );
  ~BGPConnection();

  bool connect();
  bool disconnect();

  bool sendMsg( const std::string& ) const;
  bool sendMsgSync( const std::string& cmd, std::string& retval );
  bool recvMsg( std::string& );
  void setPort(const int port);

  std::string getAddress() { return mBGAddress; }
  int getPort() { return mPort; }

 private:
#ifdef _WIN32
  SOCKET mServerSocket;
#else
  int mServerSocket;
#endif
  std::string mBGAddress;
  int mPort;
  sock_state_t mSocketState;
  std::string mRetVal;
};

/*************************
 * EventHandlerWrap
 ************************/
class EventHandlerWrap
{
 public:
  virtual void process( const char* ) const = 0;
  virtual ~EventHandlerWrap() {}
};

class EventHandlerWrap_Static : public EventHandlerWrap
{
 public:
  EventHandlerWrap_Static( void ( *handler )( const char* ) ):
       mSHandler( handler ) {}

  virtual ~EventHandlerWrap_Static() {}

  virtual void process( const char* c ) const
  {
    mSHandler( c );
  }

 private:
  void ( *mSHandler )( const char* );
};

template <class T> class EventHandlerWrap_NonStatic : public EventHandlerWrap
{
 public:
  EventHandlerWrap_NonStatic( T* obj, void ( T::* handler )( const char* ) ):
       mObject( obj ), mNHandler( handler ) {}

  virtual ~EventHandlerWrap_NonStatic() {}

  virtual void process( const char* c ) const
  {
    (mObject->*mNHandler)( c );
  }

 private:
  T * mObject;
  void ( T::* mNHandler )( const char* );
};

/*************************
 * P2PControl
 *************************/

/* constants for win*/
const char PLUGIN_REG_KEY[] = "HKEY_CURRENT_USER\\Software\\ACEStream";
const char BG_PATH_ELEMENT[] = "EnginePath";
const char BG_PORT_FILE[] = "acestream.port";
const char LOG_PATH_ELEMENT[] = "InstallDir";
const char LOG_FILE_NAME[] = "tsplugin.log";



/* Event Callback types */
enum bg_event_t {
	EV_NONE,
	EV_PLAY,
	EV_PLAY_AD,
	EV_PLAY_INTERRUPTABLE_AD,
	EV_PAUSE,
	EV_RESUME,
	EV_STOP,
	EV_INFO,
	EV_CLOSE,
	EV_ERROR,
	EV_AUTH,
	EV_STATUS,
	EV_STATE,
	EV_LOADRESP
};

typedef std::multimap< bg_event_t, EventHandlerWrap* > event_cb_map_t;
typedef std::pair< bg_event_t, EventHandlerWrap* > event_cb_item_t;

/* Thread parameters */
typedef struct {
	BGPConnection* connection;
    event_cb_map_t* eventMap;
#ifdef _WIN32
    HANDLE* syncEvent;
#else
    sem_t *syncEvent;
#endif

} ThreadParams;

/* States enumerations */
enum protocol_status_t {
	P_DOWN,
	P_UP,
	P_CLOSING
};

class P2PControl
{
 public:
  P2PControl( const int port = BG_PORT, const std::string bgAddress = BG_ADDRESS );
  ~P2PControl();

  bool startup();
  bool shutdown();

  bool launchBGProcess( const char * cmd = NULL );
  bool load(int type, const std::string id, const std::string sessionid, int developer_id = 0, int affiliate_id = 0, int zone_id = 0);
  bool start(int type, const std::string id, const std::string fileindexes, int developer_id = 0, int affiliate_id = 0, int zone_id = 0, int position = 0);
  bool get_player_id(const std::string infohash, int developer_id, int affiliate_id, int zone_id, std::string& response);
  bool stop();
  bool ready();
  bool duration( const std::string target, long duration);
  bool playback( const std::string target, int percent );
  bool userdata( int gender, int age );
  bool checkBG(bool);
  bool readBGconfig();

  void regEventCB( bg_event_t event, void (*callback)( const char* ) )
  {
    EventHandlerWrap * wrap = new EventHandlerWrap_Static( callback );
    mEventCBMap.insert( event_cb_item_t( event, wrap ) );
  }

  template <class T> void regEventCB( bg_event_t event, T* obj, void(T::* callback)(const char *) )
  {
    EventHandlerWrap * wrap = new EventHandlerWrap_NonStatic< T >( obj, callback );
    mEventCBMap.insert( event_cb_item_t( event, wrap ) );
  }
  void  unregEventCB( bg_event_t );

 private:
  /* Thread utilities */
  const BGPConnection* getConnection() const { return mConnection; }
  const event_cb_map_t* getEventMap() const { return &mEventCBMap; }
  /* Event Loop: receives events from BG and call the registered handlers */

#ifdef _WIN32
  static DWORD WINAPI eventLoop( LPVOID );
#else
  static void *eventLoop( void* );
#endif
  /* Back Ground Process */
  static bool startBGProcess();

  /* Variable Members */
  BGPConnection* mConnection;
  protocol_status_t mProtoState;

  CStdString enginePath;
  CStdString installDir;

  /* Thread and Sync */
  event_cb_map_t mEventCBMap;  // Association event/callback
#ifdef _WIN32
  HANDLE mEventThread; // Thread handler
  HANDLE mSyncEvent;   // Syncronize the thread at startup and shutdown time
#else
  sem_t mSyncEvent; //Syncronize the thread at startup and shutdown time
  pthread_t mEventThread; // Thread handler
#endif

};


#endif /* P2PCONTROL_H_ */
