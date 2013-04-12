/*
 * PlayListTorrent.h
 *
 *  Created on: 02.04.2013
 *      Author: ACEStream
 */
#pragma once
#ifndef PLAYLISTTORRENT_H_
#define PLAYLISTTORRENT_H_

/*
 * Example of the class,
 * that can parse response for the LOAD command
 */

#include "PlayList.h"

namespace PLAYLIST
{
	class CPlayListTorrent : public CPlayList
	{
	public:
		CPlayListTorrent(void);
		virtual ~CPlayListTorrent(void);
		virtual bool Load(const CStdString& strFileName);
		virtual bool ParseResp(const CStdString& strFileName, CStdString& response);
	};
}

#endif /* PLAYLISTTORRENT_H_ */
