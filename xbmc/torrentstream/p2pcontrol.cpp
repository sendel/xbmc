/*
* Written by TorrentStream
* see LICENSE.txt for license information
* Add Linux(pthread) support by SEN (C) VIOLONIX
*/

#include "p2pcontrol.h"

#ifdef _WIN32
	#include <Windows.h>
#else
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#define INVALID_SOCKET -1
	#define SOCKET_ERROR -1
	#include <unistd.h>
#endif

#include <sstream>
#include <fstream>
#include "ApplicationMessenger.h"

#define P_DEBUG 2

#ifdef P_DEBUG
#include <stdio.h>
#include <stdarg.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // no support
#endif // MSG_NOSIGNAL

#include "utils/log.h"

/*************************
 * BGPConnection
 *************************/
BGPConnection::BGPConnection( const int port, const std::string bgAddress )
{
    mBGAddress = bgAddress;
    mPort = port;
    mSocketState = S_DOWN;
    mRetVal = "";
}

void BGPConnection::setPort(const int port)
{
	disconnect();
	mPort = port;
}

BGPConnection::~BGPConnection()
{
    disconnect();
}


bool BGPConnection::connect()
{
#ifdef _WIN32
    WSADATA wsaData;
#endif
    struct sockaddr_in serverAddress;
    int iResult;

    /* Just to be in a consistent state */
    if( mSocketState == S_UP )
        return true;
    else if( mSocketState != S_DOWN )
        disconnect();

#ifdef _WIN32
    /* Init Winsock */
    iResult = WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
    if( iResult != 0 )
        return disconnect();
#endif

    mSocketState = S_STARTED;

    /* Create socket */
    if( ( mServerSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP ) ) == INVALID_SOCKET )
        return disconnect();

    mSocketState = S_CREATED;

    memset( &serverAddress, 0, sizeof( serverAddress ) );
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons( mPort );
    serverAddress.sin_addr.s_addr = inet_addr( mBGAddress.c_str() );

    /* Connect to the BG process server */
    if( ( ::connect( mServerSocket, (struct sockaddr *)&serverAddress, sizeof( serverAddress ) ) ) == SOCKET_ERROR )
    {
        CLog::Log(LOGERROR, "BGPConnection: Could not connect to server" );
#ifdef _WIN32
        CLog::Log(LOGERROR, "%i Socket Server : %i Server Addres : %i", WSAGetLastError(), mServerSocket, serverAddress.sin_addr.s_addr );
#else

#endif
        return disconnect();
    }
    CLog::Log(LOGERROR,"connection=%u: BGPConnection: CONNECTED", this);
    mSocketState = S_UP;
    return true;
}

bool BGPConnection::disconnect()
{
    /* Shut down the connection depending on the state of the socket
       It basically returns always false (stupid code optimization reasons)
	*/
#ifdef P_DEBUG_2
    char * tmp_str = NULL;
    sprintf( tmp_str, "BGPConnection: Shutting down connection from status %d", mSocketState );
    CLog::Log(LOGERROR, tmp_str );
#endif

    if( mSocketState == S_DOWN )
        return false;

    if( mSocketState >= S_CREATED )
    {
#ifdef _WIN32
        closesocket( mServerSocket );
#else
        close( mServerSocket );
#endif
        mServerSocket = INVALID_SOCKET;
    }

#ifdef _WIN32
    WSACleanup();
#endif
    mSocketState = S_DOWN;
    return false;
}

bool BGPConnection::sendMsg( const std::string& event ) const
{
    int res;

    /* Check socket status */
    if( mSocketState != S_UP )
        return false;

    /* Complete message */
    std::string msg = event + "\r\n";
    /* Send Event */
    res = send( mServerSocket, msg.c_str(), msg.length(), MSG_NOSIGNAL );
    CLog::Log(LOGNOTICE, "connection=%u: BGPConnection: Sending: %s", this, event.c_str() );
    if( res == SOCKET_ERROR )
	{
        CLog::Log(LOGERROR, "connection=%u: BGPConnection: Error in sending event", this );
        return false;
    }

    return true;
}

bool BGPConnection::sendMsgSync( const std::string& cmd, std::string& retval )
{
    mRetVal = "";
    if( !sendMsg(cmd))
        return false;

    int tries = 50;
    while(tries > 0) {
        if(mRetVal.length())
		{
            CLog::Log(LOGNOTICE,"connection=%u: BGPConnection::sendMsgSync: got retval %s", this, mRetVal.c_str());
            break;
        }
#ifdef _WIN32
        Sleep(100);
#else
        usleep(1000);
#endif
        --tries;
    }

    if(mRetVal.length() == 0)
	{
        CLog::Log(LOGNOTICE,"connection=%u: BGPConnection::sendMsgSync: no retval", this);
        return false;
    }

    if(mRetVal == "[empty]")
		mRetVal = "";

    retval = mRetVal;
    return true;
}

bool BGPConnection::recvMsg( std::string& msg )
{
    char inBuffer[IN_BUF_LEN];
    int  res;

    if( mSocketState != S_UP )
	{
        CLog::Log(LOGNOTICE,"connection=%u: BGPConnection: mSocketState != S_UP: set command to SHUTDOWN", this);
        msg = "SHUTDOWN";
        return false;
    }

    msg = "";
    while( true )
    {
        res = recv( mServerSocket, inBuffer, IN_BUF_LEN, 0 );

        if( res <= 0 )
        {
            CLog::Log(LOGERROR,"connection=%u: BGPConnection: Error in receiving stream", this);
            msg = "SHUTDOWN";
            return false;
        }

        msg.append( inBuffer, res );
        if( ! msg.compare( msg.size() - 2, 2, "\r\n" ) )
		{
            // Trim "\r\n"
            msg.erase( msg.size() - 2 );
            if( !msg.compare(0, 2, "##"))
			{
                mRetVal = msg.substr(2);
                if( mRetVal.length() == 0 )
            	    mRetVal = "[empty]";

                msg.clear();
                CLog::Log(LOGNOTICE,"connection=%u: BGPConnection: retval : %s", this, mRetVal.c_str());
            }
            else
                break;
        }
    }

    if(msg.compare(0, 4, "STATUS") != 0 && msg.compare(0, 4, "AUTH") != 0)
        CLog::Log(LOGNOTICE,"connection=%u: BGPConnection: RECEIVED : %s", this, msg.c_str());

    return true;
}

/*************************
 * P2PControl
 *************************/
P2PControl::P2PControl( const int port, const std::string bgAddress ) :
     mEventCBMap(), mProtoState( P_DOWN )
{
    mConnection = new BGPConnection( port, bgAddress );
    mEventThread = NULL;
}

P2PControl::~P2PControl()
{
    shutdown();
    delete mConnection;
}

bool P2PControl::checkBG(bool startup)
{
    if( !mConnection->connect() )
	{
    	mConnection->disconnect();
    	if(!readBGconfig())
    	{
    		CLog::Log(LOGERROR,"read config error... Check your version of ACEStream (http://www.acestream.org)  and update if old!");
    		return false;
    	}

        if( !startBGProcess())
        {
        	return false;
        }
        int i=10; //tries
        bool conn=false;
        while((conn=mConnection->connect())==false && i>0)
        {
        	//std::cout << conn << std::endl;
        	mConnection->disconnect();
#ifdef _WIN32
        	Sleep(1000);
#else
        	sleep(1);
#endif

        	if(!readBGconfig()) //check port in file
        	{
        		CLog::Log(LOGERROR,"read config error... Check your version of ACEStream (http://www.acestream.org)  and update if old!");
        		return false;
        	}

        	i--;
        }

        if(i<=0 && !conn) return false;
    }
    return true;
}

/*
 * connecting to bg process and starting main loop
 */
bool P2PControl::startup()
{
    CLog::Log(LOGNOTICE, "P2PControl::starting up..." );

    /* Try to connect to the BG Process.
       If it doesn't succeed then try to
       start the BG Process and try to
       connect againg
	*/
    if( mProtoState == P_UP )
        return true;

    if(!checkBG(true))
	{
            return mConnection->disconnect();
    }
    else
	{
        /* Already connected, send hello request to check connection */
        bool success = false;
        std::string hello_req = "HELLOBG version=3";
        std::string hello_resp = "";
        std::string expected_resp = "HELLOTS";
        if(!mConnection->sendMsg(hello_req))
            CLog::Log(LOGERROR,"P2PControl::startup: failed to send hello");
        else if(!mConnection->recvMsg(hello_resp))
            CLog::Log(LOGERROR,"P2PControl::startup: failed to receive hello respose");
        else if(hello_resp.find(expected_resp) >= 0)
        {
            CLog::Log(LOGERROR,"P2PControl:startup: incorrect hello response. Expected %s. Received %s.", expected_resp.c_str(), hello_resp.c_str());
            success = true;
        }
        else
		{
            CLog::Log(LOGNOTICE,"P2PControl::startup: got hello from bg");
            success = true;
        }

        if( !success)
		{
                return mConnection->disconnect();
        }
    }

#ifdef _WIN32
    if( mEventThread != NULL ) // TODO : confusing... fix startup procedure
        return true;
    /* Init syncronization event */
    mSyncEvent = CreateEvent( NULL, false, false, NULL );
    if( mSyncEvent == NULL )
    {
        CLog::Log(LOGERROR, "P2PControl: Error in creating syncronization event" );
        return false;
    }
    /* Start the Event Loop */
    ThreadParams* threadParams = new ThreadParams;
    threadParams->connection = mConnection;
    threadParams->eventMap = &mEventCBMap;
    threadParams->syncEvent = &mSyncEvent;

    mEventThread = CreateThread( NULL, 0, &eventLoop, threadParams, 0, NULL );

    if( mEventThread == NULL )
    {
        CLog::Log(LOGERROR, "P2PControl: Could not start event loop thread" );
        delete threadParams;
        return false;
    }

    if( WaitForSingleObject( mSyncEvent, INFINITE ) != WAIT_OBJECT_0 )
        CLog::Log(LOGERROR, "Sync Error while Starting up the event thread" );

#else
    //??? mEventThread is tid of thread,
    if( mEventThread != 0 ) // TODO : confusing... fix startup procedure
        return true;
    sem_init(&mSyncEvent,0,0);

    ThreadParams* threadParams = new ThreadParams;
    threadParams->connection = mConnection;
    threadParams->eventMap = &mEventCBMap;
    threadParams->syncEvent = &mSyncEvent;

   //Start events thread
   if(pthread_create(&mEventThread,NULL,eventLoop,(void *)threadParams))
   {
	   CLog::Log(LOGERROR, "P2PControl: Could not start event loop thread" );
       delete threadParams;
       return false;
   }

   //Wait for start process
   sem_wait(&mSyncEvent);

#endif


    delete threadParams;
    mProtoState = P_UP;
    return true;
}

/*
 * shut down bgprocess
 */
bool P2PControl::shutdown()
{

    mConnection->sendMsg( "SHUTDOWN" );
#ifdef _WIN32
    DWORD res;
    //its not good way if BG was shutdown.
    res = WaitForSingleObject( mSyncEvent, 2000 );
    if( res == WAIT_TIMEOUT )
        CLog::Log(LOGERROR,"connection=%u: Sync Error while closing thread", mConnection);
    else
        CLog::Log(LOGNOTICE,"connection=%u: P2PControl: Thread cleanly exited", mConnection);
    CloseHandle( mEventThread );
    CloseHandle( mSyncEvent );

#else
 //linux thread waiting
   /* struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    	CLog::Log(LOGERROR,"clock_gettime");
    ts.tv_sec += 2; //wait 2000ms

    sem_timedwait(&mSyncEvent, &ts);

    if (errno == ETIMEDOUT)
    	CLog::Log(LOGERROR,"connection=%u: Sync Error while closing thread", mConnection);
    else
    	CLog::Log(LOGNOTICE,"connection=%u: P2PControl: Thread cleanly exited", mConnection);*/
    pthread_join(mEventThread,NULL);
    sem_destroy(&mSyncEvent);
    CLog::Log(LOGNOTICE,"connection=%u: P2PControl: Thread cleanly exited", mConnection);
    //pthread_cancel(mEventThread);
    //pthread_kill(mEventThread, 9); //may be need for force close thread
#endif
    mProtoState = P_DOWN;

    return true;
}


/*
 * Read config for Win32 AceStream, may be fo linux for future
 */
#define ACESTREAM_BUFFER_SIZE 4096
bool P2PControl::readBGconfig()
{
#ifdef _WIN32
    LONG result;
    HKEY hKey;
    CHAR BGpath[ACESTREAM_BUFFER_SIZE]; // TODO : Fix this
    DWORD bufLen = ACESTREAM_BUFFER_SIZE;

    /* Look in the Windows registry to get the path of the BG */
    result = RegOpenKeyEx( HKEY_LOCAL_MACHINE, PLUGIN_REG_KEY, 0, KEY_QUERY_VALUE, &hKey );
    if( result != ERROR_SUCCESS )
	{
    	CLog::Log(LOGERROR,"Can't open ACEEngine key %s", PLUGIN_REG_KEY);
		RegCloseKey( hKey );
		return false;
	}
    result = RegQueryValueEx( hKey, BG_PATH_ELEMENT, NULL, NULL, (LPBYTE)BGpath, &bufLen);
    if( result != ERROR_SUCCESS )
	{
    	CLog::Log(LOGERROR,"Can't get ACEEngine path %s", BG_PATH_ELEMENT);
		RegCloseKey( hKey );
		return false;
	}

   enginePath = BGpath;


    result = RegQueryValueEx( hKey, LOG_PATH_ELEMENT, NULL, NULL, (LPBYTE)BGpath, &bufLen);
    if( result != ERROR_SUCCESS )
	{
    	CLog::Log(LOGERROR,"Can't get ACEEngine install dir %s", LOG_PATH_ELEMENT);
		RegCloseKey( hKey );
		return false;
	}

    installDir = BGpath;

    RegCloseKey( hKey );

    //get port
    std::string line;
    //std::ifstream portfile("file.txt");
    //std::stringstream buffer;
    //buffer << portfile.rdbuf();
    std::string filename(installDir+"\\"+BG_PORT_FILE);
    std::ifstream portfile;
    portfile.open(filename.c_str(),std::ifstream::in);
    	if(!portfile.good())
    	{
    		CLog::Log(LOGERROR,"Can't get ACEEngine port file %s", filename.c_str());
    		return false;
    	}

    	while(!portfile.eof())
    	{
    		std::getline(portfile, line);
    		CStdString cline=line;
    		cline.Trim();
    		if(!cline.IsEmpty())
    		{
    			std::istringstream buffer(cline);
    			int value;
    			if((buffer >> value).fail())
    			{
    			      //ERROR
    	    		CLog::Log(LOGERROR,"Can't get ACEEngine port %s", cline.c_str());
    	    		portfile.close();
    	    		return false;
    			}
    			CLog::Log(LOGNOTICE,"ACEEngine port is %i", value);
    			mConnection->setPort(value);
    			break;
    		}

    	}
    	portfile.close();

    	return true;

#else

    	return true;
#endif
}

/*
 * start up bgprocess
 */
bool P2PControl::startBGProcess()
{
	//need kill all instance before start

	bool started=false;
#ifdef _WIN32


    /* Set up variables */
    STARTUPINFOA startupInfo;
    PROCESS_INFORMATION processInfo;
    memset( &startupInfo, 0, sizeof( startupInfo ) );
    memset( &processInfo, 0, sizeof( processInfo ) );
    startupInfo.cb = sizeof( startupInfo );

    CLog::Log(LOGNOTICE, "Starting BG Process..." );
    /* Finally start the BG Process */
    started = CreateProcess( enginePath.c_str(),
        NULL,
        NULL,
        NULL,
        false,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &startupInfo,
        &processInfo );

    /* Wait the process to startup and send a 'startup' event */
    HANDLE startupEvent = CreateEvent( NULL, false, false, "startupEvent" );
    if( startupEvent == NULL )
    {
        CLog::Log(LOGERROR, "P2PControl: Error in creating syncronization event: Could not create BG Process" );
        started = false;
    }
    else
    {
        if( started )
        {
            CLog::Log(LOGNOTICE, "Waiting Startup event from BG" );
            ::WaitForSingleObject( startupEvent, INFINITE );
            CLog::Log(LOGNOTICE, "BGProcess Created" );
        }
        else
        {
            CLog::Log(LOGERROR, "Could not start BG Process" );
            CLog::Log(LOGERROR,  "Last Error code : %i", GetLastError() );
        }

        CloseHandle( startupEvent );
    }

    CloseHandle( processInfo.hProcess );
    CloseHandle( processInfo.hThread );
#else
    //bgstart by linux
    //if haven't BG in process_list try start it...
    //port 62062
    //connect to bg_port if not try start /usr/bin/acestreamengine-client-gtk
	int pid, status;
	CLog::Log(LOGNOTICE, "Starting BG Process..." );
	system("/usr/bin/acestreamengine-client-gtk &");

	//CApplicationMessenger::Get().ExecOS("/usr/bin/acestreamengine-client-gtk",0);
	sleep(2);
	started=true;

#endif
    return started;
}

/*
 * tell bgprocess to load source
 * Input params:
 * type : type of loading source (torrent url, infohash, player id, raw )
 * id : identifier of type (ex. if type=TS_ID_TORRENT_URL if can be like file:///c://tmp//test.torrent)
 * response : output param
 * developer_id : developer identifier
 * affiliate_id : affiliate identifier
 * zone_id : zone identifier
 * Returns true or false
 *
 * example of response :
 * {"status": 2, "files": [["01%20-%20Track%201.avi", 0], ["02%20-%20Track%202.avi", 1], ["03%20-%20Track%202.avi", 2]], "infohash": "617b9b1f5dd37dasd0d3f740413dacdf7b0fb8ee"}
 * "status" : 0 - has no media content, 1 - one media, 2 - playlist
 */

/*
 * LOADSYNC by SEN
 */
bool P2PControl::load(int type, const std::string id, const std::string sessionid, int developer_id, int affiliate_id, int zone_id)
{
    std::stringstream command;
    command << "LOADASYNC " << sessionid << " ";
    if(type == TS_ID_TORRENT_URL)
        command << "TORRENT";
    else if(type == TS_ID_INFOHASH)
        command << "INFOHASH";
    else if(type == TS_ID_PLAYER)
        command << "PID";
    else if(type == TS_ID_RAW)
        command << "RAW";
    else
        return false;
    command << " " << id;

    if(type != TS_ID_PLAYER)
	{
        command << " " << developer_id;
        command << " " << affiliate_id;
        command << " " << zone_id;
    }

    return mConnection->sendMsg(command.str());
}

/*
 * tell bgprocess to return player id by infohash
 * Input params:
 * infohash : infohash of source
 * developer_id : developer identifier
 * affiliate_id : affiliate identifier
 * zone_id : zone identifier
 * response : returning string
 * Returns true or false
 */
bool  P2PControl::get_player_id(const std::string infohash, int developer_id, int affiliate_id, int zone_id, std::string& response)
{
#ifdef P_DEBUG_2
    CLog::Log(LOGERROR,"P2PControl:get_player_id: infohash=%s d=%d a=%d z=%d", infohash.c_str(), developer_id, affiliate_id, zone_id);
#endif

    std::stringstream command;
    command << "GETPID " << infohash << " " << developer_id << " " << affiliate_id << " " << zone_id;

    return mConnection->sendMsgSync(command.str(), response);
}

/*
 * tell bgprocess to start downloading
 * Input params:
 * type : type of loading source (torrent url, infohash, player id, raw )
 * id : identifier of type (ex. if type=TS_ID_TORRENT_URL if can be like file:///c://tmp//test.torrent)
 * fileindexes : comma-separated string, includes indexes of media from source (ex. 0,1,2,3 or 0)
 * developer_id : developer identifier
 * affiliate_id : affiliate identifier
 * zone_id : zone identifier
 * Returns true or false
 */
bool P2PControl::start(int type, const std::string id, const std::string fileindexes, int developer_id, int affiliate_id, int zone_id, int position)
{
#ifdef P_DEBUG_2
    CLog::Log(LOGERROR,"P2PControl:start: type=%d id=%s idx=%s d=%d a=%d z=%d p=%d", type, id.c_str(), fileIndexed.c_str(), developer_id, affiliate_id, zone_id, position);
#endif

    /* Send START command with the torrent */
    std::stringstream command;
    command << "START ";
    if(type == TS_ID_DIRECT_URL)
        command << "URL";
    else if(type == TS_ID_TORRENT_URL)
        command << "TORRENT";
    else if(type == TS_ID_INFOHASH)
        command << "INFOHASH";
    else if(type == TS_ID_PLAYER)
        command << "PID";
    else if(type == TS_ID_RAW)
        command << "RAW";
    else
        return false;

    command << " " << id;
	if(type != TS_ID_DIRECT_URL && fileindexes!="") {
        command << " " << fileindexes;
    }
    if(type != TS_ID_PLAYER)
	{
        command << " " << developer_id;
        command << " " << affiliate_id;
        command << " " << zone_id;
    }
    if(position != 0)
        command << " " << position;

    return mConnection->sendMsg(command.str());
}

/*
 * tell bgprocess to stop downloading
 */
bool P2PControl::stop()
{
    return mConnection->sendMsg("STOP");
}

/*
 * tell bgprocess that plugin has started and ready to receive events
 */
bool P2PControl::ready()
{
    return mConnection->sendMsg("READY");
}

/*
 * tell bgprocess the duration of target media
 * Input params:
 * target : url of playing media
 * duration : duration
 * Returns true/false
 */
bool  P2PControl::duration( const std::string target, long duration)
{
    CLog::Log(LOGNOTICE, "P2PControl: Sending duration." );
	std::stringstream _stream;
	_stream << duration;
	std::string command = "DUR " + target + " " + _stream.str();
	return mConnection->sendMsg(command);
}

/*
 * tell bgprocess the target media playback percent
 * Input params:
 * target : url of playing media
 * percent : playback percent (only 0, 25, 50, 75, 100)
 */
bool  P2PControl::playback( const std::string target, int percent )
{
    CLog::Log(LOGNOTICE, "P2PControl: Sending playback: %i%%", percent );
	std::stringstream _stream;
	_stream << percent;
	std::string command = "PLAYBACK " + target + " " + _stream.str();
	return mConnection->sendMsg(command);
}

/*
 *  bgprocess set userdata (api ver=3)
 */
bool P2PControl::userdata( int gender, int age )
{
	CLog::Log(LOGNOTICE, "P2PControl: Sending userdata: %i %i", gender, age );
	CStdString command("");
	command.Format("USERDATA [{\"gender\": %i}, {\"age\": %i}]",gender, age);
	std::cout << command << std::endl;
	return mConnection->sendMsg(command);
}


/*
 * main loop
 */
#ifdef _WIN32
DWORD WINAPI P2PControl::eventLoop( LPVOID params )
#else
void *P2PControl::eventLoop( void* params )
#endif

{

#ifndef _WIN32
	//need for the pthread close right
	//pthread_detach(pthread_self());
	//pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	//pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif

    BGPConnection* connection;
    event_cb_map_t* eventMap;
    std::string rawServerCmd, serverCmd, command;
    size_t delimiterPos;
    bool isLastCommand, exitEventLoop;
    bg_event_t event;
    event_cb_map_t::iterator eventIt;
    /* Just for iterating through the events */
    std::pair< event_cb_map_t::iterator, event_cb_map_t::iterator > equalEventsIt;

#ifdef _WIN32
    HANDLE* syncEvent;
#else
    sem_t *syncEvent;
#endif


    /* Parse parameters */
    connection = ( (ThreadParams *)params )->connection;
    eventMap = ( (ThreadParams *)params )->eventMap;
    syncEvent = ( (ThreadParams *)params )->syncEvent;
#ifdef _WIN32
    SetEvent( *syncEvent ); /* Main thread can delete ThreadParams now */
#else
    //may be try to use pthread_condition()
    sem_post(syncEvent);
#endif

    CLog::Log(LOGNOTICE,"connection=%u: P2P Thread: STARTING LOOP", connection);
    /* Main Thread Loop */
    exitEventLoop = false;
    while( !exitEventLoop )
    {
        if( ! connection->recvMsg( rawServerCmd ) )
        {
            CLog::Log(LOGERROR,"connection=%u: P2P Thread: Unable to receive the command from BG", connection);
            if(rawServerCmd != "SHUTDOWN")
            {
            	//sleep
                continue;
            }
        }

        isLastCommand = false;
        while(!isLastCommand)
		{

            delimiterPos = rawServerCmd.find("\r\n");
            if(delimiterPos == std::string::npos)
			{
                serverCmd = rawServerCmd;
                isLastCommand = true;
            }
            else
			{
                serverCmd = rawServerCmd.substr(0, delimiterPos);
                rawServerCmd.erase(0, delimiterPos + 2);
                if(rawServerCmd.size() == 0)
                    isLastCommand = true;
            }

            command = "";
            event   = EV_NONE;
			/* NOT USED FROM API 3.0
			 *
			 * if( ! serverCmd.compare( 0, 7, "PLAYADI" ) )
            {
                command = serverCmd.substr( 8 );
                event = EV_PLAY_INTERRUPTABLE_AD;
            }
            else if( ! serverCmd.compare( 0, 6, "PLAYAD" ) )
            {
                command = serverCmd.substr( 7 );
                event = EV_PLAY_AD;
            }
            else*/
            if( ! serverCmd.compare( 0, 5, "START" ) )
            {
                command = serverCmd.substr( 6 );
                event = EV_PLAY;
            }
            else if ( ! serverCmd.compare( 0, 5, "PAUSE" ) )
            {
                event = EV_PAUSE;
            }
            else if ( ! serverCmd.compare( 0, 6, "RESUME" ) )
            {
                event = EV_RESUME;
            }
            else if ( ! serverCmd.compare( 0, 8, "SHUTDOWN" ) )
            {
                CLog::Log(LOGNOTICE, "P2P Thread: Received SHUTDOWN" );
                event = EV_CLOSE;
            }
            else if ( ! serverCmd.compare( 0, 4, "INFO" ) )
            {
                command = serverCmd.substr( 5 );
                event = EV_INFO;
            }
            else if ( ! serverCmd.compare( 0, 5, "ERROR" ) )
            {
                command = serverCmd.substr( 6 );
                event = EV_ERROR;
            }
            else if ( ! serverCmd.compare( 0, 4, "AUTH" ) )
            {
                command = serverCmd.substr( 5 );
                event = EV_AUTH;
            }
            else if ( ! serverCmd.compare( 0, 6, "STATUS" ) )
            {
                command = serverCmd.substr( 7 );
                event = EV_STATUS;
            }
			else if ( ! serverCmd.compare( 0, 5, "STATE" ) )
            {
                command = serverCmd.substr( 6 );
                event = EV_STATE;
            }
			else if ( ! serverCmd.compare( 0, 8, "LOADRESP" ) )
            {
                command = serverCmd.substr( 9 );
                event = EV_LOADRESP;
            }
            else
            {
                CLog::Log(LOGERROR,"connection=%u: P2P Thread: Received wrong command: %s", connection, serverCmd.c_str());
                continue;
            }

            if(event != EV_STATUS && event != EV_AUTH)
                CLog::Log(LOGNOTICE,"connection=%u: P2P Thread: Command: %s", connection, command.c_str());



            equalEventsIt = eventMap->equal_range( event );
            for( eventIt = equalEventsIt.first; eventIt != equalEventsIt.second; ++eventIt )
                ( (*eventIt).second )->process( command.c_str() );


            if(event == EV_CLOSE)
			{
                CLog::Log(LOGERROR,"connection=%u: received EV_CLOSE: break thread loop", connection);
                exitEventLoop = true;
                break;
            }

            //there sleep need
        }
    }
    CLog::Log(LOGNOTICE,"connection=%u: P2P Thread: EXITING Loop", connection);
#ifdef _WIN32
    SetEvent( *syncEvent ); /* Main thread can shut down now */
    return 0;
#else
    //sem_post(syncEvent);
    pthread_exit(NULL);
    return NULL;
#endif


}

/*
 * unregister event callback
 */
void P2PControl::unregEventCB( bg_event_t event )
{
    mEventCBMap.erase( event );
}
