#include "torrentstream.h"
#include "utils/URIUtils.h"
#include "PlayListPlayer.h"
#include "URL.h"
#include "utils/log.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/CharsetConverter.h"
#include "playlists/PlayListTorrent.h"
#include "ApplicationMessenger.h"
#include <stdlib.h>
#include <iostream>
CTorrentStream::CTorrentStream(void)
{
	p_p2pcontrol = NULL;
	Clear();
}

CTorrentStream::~CTorrentStream(void)
{
	Clear();
	Stop();
	if(p_p2pcontrol)
		delete p_p2pcontrol;
}

/*
 * Example of initialization
 */
bool CTorrentStream::Initialize()
{
	srand (time(NULL));
	if(isInitialized()) {
		delete p_p2pcontrol;
		p_p2pcontrol = NULL;
	}

	/* creating new instanse of P2PControl */
	p_p2pcontrol = new P2PControl();
	/* trying to connect */
	if( !p_p2pcontrol->startup() )
		return false;

	/* registering event callbacks */
	p_p2pcontrol->regEventCB< CTorrentStream >( EV_PLAY, this, &CTorrentStream::onP2PPlay );
	p_p2pcontrol->regEventCB< CTorrentStream >( EV_PLAY_AD, this, &CTorrentStream::onP2PPlayAd );
	p_p2pcontrol->regEventCB< CTorrentStream >( EV_PLAY_INTERRUPTABLE_AD, this, &CTorrentStream::onP2PPlayInterruptableAd );
    p_p2pcontrol->regEventCB< CTorrentStream >( EV_PAUSE, this, &CTorrentStream::onP2PPause );
    p_p2pcontrol->regEventCB< CTorrentStream >( EV_RESUME, this, &CTorrentStream::onP2PResume );
    p_p2pcontrol->regEventCB< CTorrentStream >( EV_STOP, this, &CTorrentStream::onP2PStop );
    p_p2pcontrol->regEventCB< CTorrentStream >( EV_INFO, this, &CTorrentStream::onP2PInfo );
    p_p2pcontrol->regEventCB< CTorrentStream >( EV_ERROR, this, &CTorrentStream::onP2PError );
    p_p2pcontrol->regEventCB< CTorrentStream >( EV_AUTH, this, &CTorrentStream::onP2PAuth );
	p_p2pcontrol->regEventCB< CTorrentStream >( EV_STATUS, this, &CTorrentStream::onP2PStatus );
	p_p2pcontrol->regEventCB< CTorrentStream >( EV_STATE, this, &CTorrentStream::onP2PState );
	p_p2pcontrol->regEventCB< CTorrentStream >( EV_LOADRESP, this, &CTorrentStream::onP2PLoad );
	p_p2pcontrol->regEventCB< CTorrentStream >( EV_CLOSE, this, &CTorrentStream::onP2PClose );


	/* we are ready to work */
	return p_p2pcontrol->ready();
}

/*
 * Check ACE Engine for work and start/reinit if not
 */
/*bool CTorrentStream::CheckBGService()
{

}
*/

/*
 * Example of wrapper for command LOAD
 * Input params:
 * id : identifier of source (ex. C:\test.torrent )
 * response : returning string
 */
bool CTorrentStream::Load( const std::string id )
{
	if( !p_p2pcontrol )
		return false;
	char buf[256];
	snprintf(buf,255,"%i",rand());
	ui_seesionid = buf;


	/* checking id */
	CStdString _id("");
	_id.assign(CSpecialProtocol::TranslatePath(id));


	/* checking for type of id (only for TS_ID_TORRENT_URL and TS_ID_PLAYER) */
	int i_type = URIUtils::IsTorrent(_id) ? TS_ID_TORRENT_URL : ( URIUtils::IsTorrentStreamPID(_id) ? TS_ID_PLAYER : -1 );
	/* checking for "file:///" prefix */

	if( i_type == TS_ID_TORRENT_URL )
	{
		if(id.substr(0, 8).compare("file:///")==0)
		{
			_id=id.substr(8 );
			CURL::Encode(_id);
			_id.insert(0, "file:///");

		}
		else
			if(_id.substr(0, 7).compare("http://")==0 )
			{

			}
			else
			{
				CURL::Encode(_id);
				_id.insert(0, "file:///");
			}


	}
	std::cout << "load __t_id(): " << _id << std::endl;
	if(i_type == TS_ID_PLAYER)
	{
		_id=URIUtils::GetFileName(id);
	}



	std::cout << "load _t_id: " << _id << std::endl;
	psz_playlist_path = id;
	/* executing load command */
	bool ret = p_p2pcontrol->load( i_type, _id, ui_seesionid, 6 );

	//Restart Engine
	if(!ret) {
		CLog::Log(LOGERROR,"Load: restart engine after error");
		Clear();
		Initialize();
	}

	return ret;


}

/*
 * Example of wrapper for command START
 * Input: item to process
 */
bool CTorrentStream::Start( CFileItemPtr item , int playlist)
{
	/* initialization */
	if( !isInitialized() )
	{
		if( !Initialize() )
		{
			CLog::Log(LOGERROR, "%s - Cannot initialize bgprocess!", __FUNCTION__);
			return false;
		}
		SendUserData( 1, 3 );
	}

	if( !p_p2pcontrol )
		return false;
	p_current_item = item;

	currentPlayList = playlist;


	CStdString id = CSpecialProtocol::TranslatePath(p_current_item->GetPath());

	std::cout << "start t_id: " << id << std::endl;

	CStdString path("");
	CStdString indexes("");

	/* on parsing LOAD command response we added to all pathes prefix torrentstream: (torrentstream - is for example)
	 * this prefix we erase
	 */

	if(!item->IsLoadedTorrent())
	{
		Load(item->GetPath());
		return false;
	}

	int pos = 0;
	if( (pos=id.Find("torrentstream:")) >= 0 )
		id.Delete(pos, 14);
	if( (pos=id.rfind(" ")) >= 0 )
	{
		path = id.substr( 0, pos );
		indexes = id.substr( pos );
	}
	else
		path = id;

	CStdString _id(path);
	int i_type = URIUtils::IsTorrent(path) ? TS_ID_TORRENT_URL : ( URIUtils::IsTorrentStreamPID(path) ? TS_ID_PLAYER : -1 );

	if( i_type == TS_ID_TORRENT_URL )
	{
		if(path.substr(0, 8).compare("file:///")==0)
		{
			_id=path.substr(8 );
			CURL::Encode(_id);
			_id.insert(0, "file:///");
		}
		else if(path.substr(0, 7).compare("http://") ==0 )
		{

		}
		else
		{
			CURL::Encode(_id);
			_id.insert(0, "file:///");
		}

	}

	if(i_type == TS_ID_PLAYER)
	{
		_id=URIUtils::GetFileName(path);
	}





	if(indexes!="")
		_id.append(indexes);
	std::cout << "__t_id: " << _id << std::endl;
	/* executing START command ( param fileindexes=="" because all indexes we are already in _id)  and set flags*/
	b_started = b_waiting_main_content = p_p2pcontrol->start(i_type, _id, "", 6);

	if(!b_started) {
		CLog::Log(LOGERROR,"Start: restart engine after error");
		Initialize();
	}

	return b_started;
}


/*
 * command USERDATA
 * Input:
 * gender : 1- man, 2 - woman
 * age : 1 - less 13 year
		 2 - 13-17
 	 	 3 - 18-24
	 	 4 - 25-34
		 5 - 35-44
		 6 - 45-54
		 7 - 55-64
		 8 - more 64
 */
void CTorrentStream::SendUserData( int gender, int age )
{
	if( !p_p2pcontrol )	return;
	bool ret = p_p2pcontrol->userdata(gender, age);
	//Restart Engine
	if(!ret) {
		CLog::Log(LOGERROR,"Load: restart engine after error");
		Initialize();
	}

	return;
}


/*
 * command PLAYBACK
 * Input:
 * offset : current playback time
 * duration : total duration  time
 */
void CTorrentStream::SendPlayback( long offset, long duration )
{
	if( !p_p2pcontrol )	return;
	float persentage = offset * 100 / (float)duration;

	if(!b_playback_0 && persentage>=0 && persentage<25)
	{
		p_p2pcontrol->playback(p_current_item->GetPath(), 0);
		b_playback_0 = true;
	}
	if(!b_playback_25 && persentage>=25 && persentage<50)
	{
		p_p2pcontrol->playback(p_current_item->GetPath(), 25);
		b_playback_25 = true;
	}
	if(!b_playback_50 && persentage>=50 && persentage<75)
	{
		p_p2pcontrol->playback(p_current_item->GetPath(), 50);
		b_playback_50 = true;
	}
	if(!b_playback_75 && persentage>=75 && persentage<100)
	{
		p_p2pcontrol->playback(p_current_item->GetPath(), 75);
		b_playback_75 = true;
	}
	if(!b_playback_100 && persentage==100)
	{
		p_p2pcontrol->playback(p_current_item->GetPath(), 100);
		b_playback_100 = true;
	}
}

/*
 * command DURATION
 */
void CTorrentStream::SendDuration( long duration )
{
	if( !p_p2pcontrol ) return;
	if(!b_duration)
	{
		p_p2pcontrol->duration(p_current_item->GetPath(), duration);
		b_duration = true;
	}
}

/*
 * command STOP download
 */
void CTorrentStream::Stop()
{
	if( !p_p2pcontrol ) return;
	p_p2pcontrol->stop();
}

/*
 * Reset flags when playback ends
 */
void CTorrentStream::ResetFlags()
{
	b_started = b_waiting_main_content = b_advertisement = b_interruptable_advertisement = false;
}

/*
 * example of private proverties getters
 */
bool CTorrentStream::isInitialized()
{
	return (p_p2pcontrol != NULL);
}

std::string CTorrentStream::p2pInfo()
{
	return psz_p2pinfo;
}

std::string CTorrentStream::p2pError()
{
	return psz_p2perror;
}

int CTorrentStream::p2pAuth()
{
	return i_p2pauth;
}

int CTorrentStream::p2pState()
{
	return i_p2pstate;
}

std::string CTorrentStream::p2pStatus()
{
	return psz_p2pstatus;
}

bool CTorrentStream::isWaitingMainContent()
{
	return b_waiting_main_content;
}

bool CTorrentStream::isStarted()
{
	return b_started;
}

bool CTorrentStream::isAdvertisement()
{
	return b_advertisement;
}

bool CTorrentStream::isInterruptableAdvertisement()
{
	return b_interruptable_advertisement;
}

/*
 * privat methods
 */
void CTorrentStream::Clear()
{
	psz_p2pinfo.clear();
	psz_p2perror.clear();
	psz_p2pstatus.clear();
	i_p2pauth = 0;
	i_p2pstate = TS_P2P_IDLE;
	ResetFlags();
	ResetPlaybackFlags();
}

void CTorrentStream::ResetPlaybackFlags()
{
	b_playback_0 = b_playback_25 = b_playback_50 = b_playback_75 = b_playback_100 = b_duration = false;
}

/*
 * example how to parse statusstr of EV_STATUS
 * statusstr looks like:
 * if main content is playing message will be like main:statusstr
 * if advertisement is playing message will be like main:statusstr|ad:statusstr
 * statusstr:
 * idle
 * err;error_id;error_message
 * check;progress
 * prebuf;progress;time;total_progress;immediate_progress;speed_down;http_speed_down;speed_up;peers;http_peers;downloaded;http_downloaded;uploaded
 * dl;total_progress;immediate_progress;speed_down;http_speed_down;speed_up;peers;http_peers;downloaded;http_downloaded;uploaded
 * buf;progress;time;total_progress;immediate_progress;speed_down;http_speed_down;speed_up;peers;http_peers;downloaded;http_downloaded;uploaded
 * wait;time;total_progress;immediate_progress;speed_down;http_speed_down;speed_up;peers;http_peers;downloaded;http_downloaded;uploaded
 */
std::string CTorrentStream::ParseStatusMessage( const char* statusstr )
{
	std::string base(statusstr);
	std::string res("");
	size_t pos;
	std::vector<std::string> values;

	pos=base.find('|');
	/* clear ad status */
	if(pos!=std::string::npos)
		base.erase(pos, base.length());

	values = split( base, ';' );
	if( values.size()<=0 )
		return res;

	if(values[0].compare("main:dl") && values[0].compare("main:idle"))
	{
		if(values[0].compare("main:buf")==0)
		{
			/* ex. Buffering 12% */
			res = "Buffering ";
			res.append(values[1]);
			res.append("%");
		}
		else if(values[0].compare("main:prebuf")==0)
		{
			/* ex. Prebuffering 12% (connected to 5 streams) */
			res = "Prebuffering ";
			res.append(values[1]);
			res.append("%");
			res.append( " (" );
			res.append( "connected to " );
			res.append(values[8]);
			if(values[8].compare("1")==0)
				res.append( " stream" );
			else
				res.append( " streams" );
			res.append(")");
		}
		else if(values[0].compare("main:check")==0)
		{
			/* ex. Checking 34% */
			res = "Checking ";
			res.append(values[1]);
			res.append("%");
		}
		else if(values[0].compare("main:wait")==0)
		{
			res = "Insufficient download speed to play without interruption";
		}
		else if(values[0].compare("main:err")==0)
		{
			res.append( values[2] );
			psz_p2perror.assign(values[2]);
		}
	}
	values.clear();

	return res;
}

/*
 * example how to parse infostr of EV_INFO
 * infostr looks like:
 * info_id;info_msg
 */
std::string CTorrentStream::ParseInfoMessage( const char* infostr )
{
	std::string base(infostr);
	std::string info("");
	std::string res("");
	int info_id = 0;
	size_t pos;

	pos = base.find(";");
	if(pos==std::string::npos)
		return base;

	/* info_id can be used for localize messages */
	info_id = atoi( base.substr(0, pos).c_str() );
	if( pos < base.length()-1 && info_id != 0 )
		info = base.substr(pos+1);

	return info;
}

/*
 * event callbacks
 */

/*
 * callback for EV_PLAY
 * example of input param :
 * http://127.0.0.1:6878/content/617b9b1f5dd37d35b053f750413dacdf7b0fb8ee/0.463829934705 pos=15 stream=1
 * [main content][ ][start position(unused)]
 */
void CTorrentStream::onP2PPlay( const char* stream )
{
	CLog::Log(LOGNOTICE, "onP2PPlay: %s", stream );

	/* pos don't use */
	std::string _stream(stream);
    size_t pos = _stream.find(" pos=");
    if(pos != std::string::npos)
		_stream = _stream.substr(0, pos);

    /*is live stream?*/
    pos = _stream.find(" stream=");
    if(pos != std::string::npos)
		_stream = _stream.substr(0, pos);



	p_current_item->SetPath(_stream);
	SendPlayback(0,1);
    CApplicationMessenger::Get().PlayFile(*p_current_item,false);

/*	if(g_playlistPlayer.Play(*p_current_item))
	{
		b_waiting_main_content = b_interruptable_advertisement = b_advertisement = false;
		ResetPlaybackFlags();
	}
*/
}

/*
 * callback for EV_PLAY_AD
 * example of input param :
 * http://127.0.0.1:6878/content/617b9b1f5dd37d35b053f750413dacdf7b0fb8ee/0.463829934705 pos=15
 * [advertisement][ ][start position(unused)]
 */
void CTorrentStream::onP2PPlayAd( const char* stream )
{
	CLog::Log(LOGNOTICE, "onP2PPlayAd: %s", stream );

	//pos don't use
	std::string _stream(stream);
    size_t pos = _stream.find(" pos=");
    if(pos != std::string::npos)
		_stream = _stream.substr(0, pos);

    p_current_item->SetPath(_stream);
	if(g_playlistPlayer.Play(*p_current_item))
	{
		b_waiting_main_content = b_advertisement = true;
		b_interruptable_advertisement = false;
		ResetPlaybackFlags();
	}
}

/*
 * callback for EV_PLAY_INTERRUPTABLE_AD
 * example of input param :
 * http://127.0.0.1:6878/content/617b9b1f5dd37d35b053f750413dacdf7b0fb8ee/0.463829934705 pos=15
 * [interruptable advertisement][ ][start position(unused)]
 */
void CTorrentStream::onP2PPlayInterruptableAd( const char* stream )
{
	CLog::Log(LOGNOTICE, "onP2PPlayInterruptableAd: %s", stream );

	//pos don't use
	std::string _stream(stream);
    size_t pos = _stream.find(" pos=");
    if(pos != std::string::npos)
		_stream = _stream.substr(0, pos);

    p_current_item->SetPath(_stream);

	if(g_playlistPlayer.Play(*p_current_item))
	{
		b_waiting_main_content = b_interruptable_advertisement = b_advertisement = true;
		ResetPlaybackFlags();
	}
}

/*
 * callback for EV_PAUSE
 */
void CTorrentStream::onP2PPause( const char* nothing )
{
	CLog::Log(LOGNOTICE, "onP2PPause: %s", nothing );
	//onP2PPause event handler
}

/*
 * callback for EV_RESUME
 */
void CTorrentStream::onP2PResume( const char* nothing )
{
	CLog::Log(LOGNOTICE, "onP2PResume: %s", nothing );
	//onP2PResume event handler
}

/*
 * callback for EV_STOP
 */
void CTorrentStream::onP2PStop( const char* nothing )
{
	CLog::Log(LOGNOTICE, "onP2PStop: %s", nothing );
	//onP2PStop event handler
}

/*
 * callback for EV_INFO
 */
void CTorrentStream::onP2PInfo( const char* infostr )
{
	CLog::Log(LOGNOTICE,"onP2PInfo: %s", infostr);
	psz_p2pinfo.assign( ParseInfoMessage(infostr) );
}

/*
 * callback for EV_ERROR
 */
void CTorrentStream::onP2PError( const char* errstr )
{
	CLog::Log(LOGNOTICE,"onP2PError: %s", errstr);
	psz_p2perror.assign( errstr );
}

/*
 * callback for EV_AUTH
 */
void CTorrentStream::onP2PAuth( const char* authstr )
{
	CLog::Log(LOGNOTICE,"onP2PAuth: %s", authstr);
    i_p2pauth = atoi(authstr);
}

/*
 * callback for EV_STATUS
 */
void CTorrentStream::onP2PStatus( const char* statusstr )
{
	CLog::Log(LOGNOTICE,"onP2PStatus: %s", statusstr);
	psz_p2pstatus.assign( ParseStatusMessage(statusstr) );
}

/*
 * callback for EV_STATE
 */
void CTorrentStream::onP2PState( const char* statestr )
{
	CLog::Log(LOGNOTICE,"onP2PState: %s", statestr);
	i_p2pstate = atoi(statestr);
	//on state3 - pause
	//on state2 - resume

}

bool CTorrentStream::parseLoad(const CStdString& strFileName, CStdString& response)
{
	int my_playlist = currentPlayList;//g_playlistPlayer.GetCurrentPlaylist();
	std::cout << "LISTNUM " << my_playlist << std::endl;
	CApplicationMessenger::Get().PlayListPlayerClear(my_playlist);
	//g_playlistPlayer.ClearPlaylist(my_playlist);


	CStdString _status( "\"status\": " );
	CStdString _files( "\"files\": " );
	CStdString _sep( "], " );
	CStdString _subsep( "\", " );
	CStdString _infohash( "\"infohash\": " );
	int _pos = 0;

	if( (_pos = response.Find(_status)) < 0 )
	{
		CLog::Log(LOGERROR, "%s - Incorrect load %s response!", __FUNCTION__, strFileName.c_str());
		return false;
	}

	/* get status form response */
	char statusVal = response.GetAt(_pos+_status.GetLength());

	if(atoi(&statusVal)!=1 && atoi(&statusVal)!=2)
	{
		CLog::Log(LOGERROR, "%s - Torrent has no media content!", __FUNCTION__);
		return false;
	}

	response.Delete(0, _pos+_status.GetLength()+3);
	response.Delete(response.GetLength()-1,1);

	if( (_pos = response.Find(_files)) < 0 )
	{
		CLog::Log(LOGERROR, "%s - Torrent has no content!", __FUNCTION__);
		return false;
	}

	response.Delete(0, _pos+_files.GetLength());

	/* erasing infohash (not use in example) */
	if( (_pos = response.Find( _infohash )) >= 0 )
		response.Delete(_pos-2, response.GetLength());

	CStdString response_copy = response;
	CStdString indexes;
	CStdString index;
	CStdString title;
	CStdString item;

	/* create indexes comma-separated string */
	for(;;)
	{
		if((_pos = response.Find( _sep )) < 0)
			_pos = response.rfind( "]]" );
		item = response.substr(1, _pos);

		if( (_pos = item.rfind( _subsep )) < 0)
			break;
		index = item.substr( _pos+_subsep.GetLength(), item.GetLength()-1-_pos-_subsep.GetLength() );
		indexes.append( index );
		indexes.append(",");
		response.Delete( 1, item.GetLength()+2 );
	}
	indexes.Delete( indexes.GetLength()-1, 1 );

	//PLAYLIST::CPlayList *list = new PLAYLIST::CPlayList(PLAYLIST_VIDEO);
	/* parsing "files" */
	CStdString to_find;
	CStdString to_start;
	int fcount = 0;
	for(;;)
	{
		if((_pos = response_copy.Find( _sep )) < 0)
			_pos = response_copy.rfind( "]]" );
		item = response_copy.substr(1, _pos);
		if((_pos = item.rfind( _subsep )) < 0)
			break;
		index = item.substr( _pos+_subsep.GetLength(), item.GetLength()-1-_pos-_subsep.GetLength() );
		title = item.substr( 2, _pos-2 );
		response_copy.Delete( 1, item.GetLength()+2 );

		g_charsetConverter.unknownToUTF8(title);

/*
 //this need for non-stop play all files in torrent.
 		to_find.clear();
		to_find.append(",");
		to_find.append(index);
		to_find.append(",");

		if((_pos = indexes.Find(to_find)) < 0)
		{
			to_find.Delete(0,1);
			if((_pos = indexes.Find(to_find)) >= 0)
				index = indexes;
		}
		else
			index = indexes.substr( _pos+1, indexes.GetLength()-1-_pos );
 //------
*/
		/* create path like:
		   torrentstream:C:\test.torrent 0,1,2
		   we add prefix torrentstream for example, to identify item to start
		*/
		to_start.clear();
		to_start.append( "torrentstream:" );
		to_start.append( strFileName );
		to_start.append( " " );
		to_start.append( index );

		CURL::Decode(title);
		//CFileItemPtr newItem(new CFileItem(title));
		CFileItem newItem(title);
        newItem.SetPath(to_start);
        std::cout << "tos " << to_start << " title " << title << std::endl;
        CApplicationMessenger::Get().PlayListPlayerAdd(my_playlist, newItem);
        //list->Add(newItem);
        //g_playlistPlayer.Add(my_playlist, newItem);
        //g_playlistPlayer.SetCurrentPlaylist(my_playlist);
        //Add(newItem);
        fcount++;
		index.clear();
	}
//	CApplicationMessenger::Get().PlayListPlayerAdd(my_playlist, (list));
	if(fcount>0)
		CApplicationMessenger::Get().PlayListPlayerPlay(0);
	//g_playlistPlayer.Add(my_playlist, (*list));
	//g_playlistPlayer.SetCurrentPlaylist(my_playlist);
	//g_playlistPlayer.Play(0);

	return true;
}


void CTorrentStream::onP2PLoad( const char* statestr )
{
	CLog::Log(LOGNOTICE,"onP2PLoadResp: %s", statestr);

	if(!statestr) {
		CLog::Log(LOGERROR,"onP2PLoadResp: havent data %s", statestr);
		return;
	}
	std::string sstr(statestr);

	CStdString csid("");
	std::cout << "STRING" << sstr << std::endl;
	CStdString playlist_json("");
	if(int pos=sstr.find(' '))
	{
		csid+=(CStdString)sstr.substr(0,pos);
		std::cout << "CSID" << csid << std::endl;
		playlist_json+=(CStdString)sstr.substr(pos+1);
		std::cout << "PLID" << playlist_json << std::endl;
	}

	CStdString mystr(ui_seesionid);


	if(csid.compare(mystr)) {CLog::Log(LOGERROR,"onP2PLoadResp:  it's not last request: %s <> %s ", csid.c_str(), mystr.c_str() ); return;};


	//PLAYLIST::CPlayList& list =  g_playlistPlayer.GetPlaylist(1);
	//std::cout << "LISTNAME " << list.GetName() << std::endl;

/*	((PLAYLIST::CPlayListTorrent&)list).ParseResp(psz_playlist_path, playlist_json);*/
	std::cout << "CURITEM " <<psz_playlist_path << std::endl;
	if(!parseLoad(psz_playlist_path, playlist_json))
	{
		CLog::Log(LOGERROR,"onP2PLoadResp: Error load playlist: %s", playlist_json.c_str());
	}

	//p_current_item->GetPath(),
	//g_application.
	//g_playlistPlayer.GetCurrentPlaylist()
	//i_p2pstate = atoi(statestr);
}

//If close when ACEStream down we try start it in next time
void CTorrentStream::onP2PClose(const char* str)
{
	CLog::Log(LOGNOTICE,"onP2PCloseEvent");
	Clear();
	if(p_p2pcontrol)
		delete p_p2pcontrol;
	p_p2pcontrol=NULL;
}
