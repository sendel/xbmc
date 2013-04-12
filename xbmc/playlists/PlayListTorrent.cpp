#include "PlayListTorrent.h"
#include "torrentstream/torrentstream.h"
#include "utils/URIUtils.h"
#include "utils/CharsetConverter.h"
#include "URL.h"
#include "utils/log.h"

using namespace PLAYLIST;

CPlayListTorrent::CPlayListTorrent(void)
{}

CPlayListTorrent::~CPlayListTorrent(void)
{}

/*
 * send LOAD command and parse response
 * example of response :
 * {"status": 2, "files": [["01%20-%20Track%201.avi", 0], ["02%20-%20Track%202.avi", 1], ["03%20-%20Track%202.avi", 2]], "infohash": "617b9b1f5dd37dasd0d3f740413dacdf7b0fb8ee"}
 */
bool CPlayListTorrent::Load(const CStdString& strFileName)
{
	/* initialization */
	if( !g_torrentStream.isInitialized() )
	{
		if( !g_torrentStream.Initialize() )
		{
			CLog::Log(LOGERROR, "%s - Cannot initialize bgprocess!", __FUNCTION__);
			return false;
		}
	}

	/* send LOAD command */
	CStdString response;
	bool b_load = g_torrentStream.Load( strFileName );

	if( !b_load )
	{
		CLog::Log(LOGERROR, "%s - Cannot load %s!", __FUNCTION__, strFileName.c_str());
		return false;
	}

	return true;
}

bool CPlayListTorrent::ParseResp(const CStdString& strFileName, CStdString& response)
{
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

	// get status form response
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

	// erasing infohash (not use in example)
	if( (_pos = response.Find( _infohash )) >= 0 )
		response.Delete(_pos-2, response.GetLength());

	CStdString response_copy = response;
	CStdString indexes;
	CStdString index;
	CStdString title;
	CStdString item;

	// create indexes comma-separated string
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


	// parsing "files"
	CStdString to_find;
	CStdString to_start;
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

		// create path like:
		// torrentstream:C:\test.torrent 0,1,2
		// we add prefix torrentstream for example, to identify item to start

		to_start.clear();
		to_start.append( "torrentstream:" );
		to_start.append( strFileName );
		to_start.append( " " );
		to_start.append( index );

		CURL::Decode(title);
		CFileItemPtr newItem(new CFileItem(title));
        newItem->SetPath(to_start);
        Add(newItem);

		index.clear();
	}

	return true;
}
