/*
 * torrentstream.h
 *
 *  Created on: 02.04.2013
 *      Author: ACEStream
 */
#pragma once
#ifndef TORRENTSTREAM_H_
#define TORRENTSTREAM_H_

/*
 * Example of wrapper for P2PControll
 */

#include "p2pcontrol.h"
#include <boost/shared_ptr.hpp>
#include "FileItem.h"
#include <vector>
#include <sstream>

//split for std::string
static std::vector<std::string> &_split(const std::string &s, char delim, std::vector<std::string> &elems)
{
    std::stringstream ss(s);
    std::string item;
    while(std::getline(ss, item, delim))
	{
        elems.push_back(item);
    }
    return elems;
}

static std::vector<std::string> split(const std::string &s, char delim)
{
	std::vector<std::string> elems;
    return _split(s, delim, elems);
}

typedef boost::shared_ptr<CFileItem> CFileItemPtr;

class CTorrentStream
{
public:
	CTorrentStream(void);
	virtual ~CTorrentStream(void);

	bool Initialize();
	bool Load( const std::string id );
	bool Start( CFileItemPtr item, int playlist );
	void SendPlayback( long offset, long duration );
	void SendDuration( long duration );
	void Stop();
	void ResetFlags();
	bool parseLoad(const CStdString& strFileName, CStdString& response);
	void SendUserData( int gender, int age );

	bool isInitialized();
	std::string p2pInfo();
	std::string p2pError();
	int p2pAuth();
	int p2pState();
	std::string p2pStatus();
	bool isWaitingMainContent();
	bool isStarted();
	bool isAdvertisement();
	bool isInterruptableAdvertisement();

	void onP2PPlay( const char* );
	void onP2PPlayAd( const char* );
	void onP2PPlayInterruptableAd( const char* );
	void onP2PPause( const char* );
	void onP2PResume( const char* );
	void onP2PStop( const char* );
	void onP2PInfo( const char* );
	void onP2PError( const char* );
	void onP2PAuth( const char* );
	void onP2PStatus( const char* );
	void onP2PState( const char* );
	void onP2PLoad( const char* );
	void onP2PClose(const char* );

private:
	void Clear();
	void ResetPlaybackFlags();
	std::string ParseInfoMessage( const char* );
	std::string ParseStatusMessage( const char* );

	P2PControl* p_p2pcontrol;  // pointer to p2pcontrol
	std::string psz_p2pinfo;  // result of EV_INFO
	std::string psz_p2perror;  // result of EV_ERROR
	std::string psz_p2pstatus;  // result of EV_STATUS
	CStdString	psz_playlist_path; //path to last playlist
	int i_p2pauth;  // result of EV_AUTH
	int i_p2pstate;  // result of EV_STATE
	CStdString ui_seesionid; //id for request
	int currentPlayList;

	CFileItemPtr p_current_item;  // shared pointer to current processing item
	bool b_started;  // if true -> start command where executed
	bool b_waiting_main_content;  // if true -> start command where executed -> waiting for EV_PLAY
	bool b_advertisement;  // if true -> EV_PLAY_AD occured -> waiting for EV_PLAY
	bool b_interruptable_advertisement;  // if true -> EV_PLAY_INTERRUPTABLE_AD occured -> waiting for EV_PLAY

	bool b_playback_0;  // playback 0% sent
	bool b_playback_25;  // playback 25% sent
	bool b_playback_50;  // playback 50% sent
	bool b_playback_75;  // playback 75% sent
	bool b_playback_100;  // playback 100% sent
	bool b_duration;  // duration sent
};

extern CTorrentStream g_torrentStream;

#endif /* TORRENTSTREAM_H_ */
