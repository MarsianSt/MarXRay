// LocatorAPI.cpp: implementation of the CLocatorAPI class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable:4995)
#include <direct.h>
#include <fcntl.h>
#include <sys\stat.h>
#pragma warning(default:4995)

#include "FS_internal.h"
#include "stream_reader.h"
#include "file_stream_reader.h"
#include "xrFS.h"
#include <filesystem>
#include <algorithm>

const u32 BIG_FILE_READER_WINDOW_SIZE	= 1024*1024;

//typedef void DUMMY_STUFF (const void*,const u32&,void*);
//XRCORE_API DUMMY_STUFF	*g_temporary_stuff = 0;

#	pragma warning(push)
#	pragma warning(disable:4995)
#	include <malloc.h>
#	pragma warning(pop)

CLocatorAPI*		xr_FS = NULL;

#ifdef _EDITOR
#	define FSLTX	"fs.ltx"
#else
#	define FSLTX	"fsgame.ltx"
#endif

struct _open_file
{
	union {
		IReader*		_reader;
		CStreamReader*	_stream_reader;
	};
	shared_str			_fn;
	u32					_used;
};

template <typename T>
struct eq_pointer;

template <>
struct eq_pointer<IReader>{
	IReader* _val;
	eq_pointer(IReader* p):_val(p){}
	bool operator () (_open_file& itm){
		return ( _val==itm._reader );
	}
};
template <>
struct eq_pointer<CStreamReader>{
	CStreamReader* _val;
	eq_pointer(CStreamReader* p):_val(p){}
	bool operator () (_open_file& itm){
		return ( _val==itm._stream_reader );
	}
};
struct eq_fname_free{
	shared_str _val;
	eq_fname_free(shared_str s){_val = s;}
	bool operator () (_open_file& itm){
		return ( _val==itm._fn && itm._reader==NULL);
	}
};
struct eq_fname_check{
	shared_str _val;
	eq_fname_check(shared_str s){_val = s;}
	bool operator () (_open_file& itm){
		return ( _val==itm._fn && itm._reader!=NULL);
	}
};

XRCORE_API xr_vector<_open_file>	g_open_files;

void _check_open_file(const shared_str& _fname)
{
	xr_vector<_open_file>::iterator it	= std::find_if(g_open_files.begin(), g_open_files.end(), eq_fname_check(_fname) );
	if(it!=g_open_files.end())
		LogInfo("%s", "file opened at least twice", _fname.c_str());
}

_open_file& find_free_item(const shared_str& _fname)
{
	xr_vector<_open_file>::iterator it	= std::find_if(g_open_files.begin(), g_open_files.end(), eq_fname_free(_fname) );
	if(it==g_open_files.end())
	{
		g_open_files.resize		(g_open_files.size()+1);
		_open_file& _of			= g_open_files.back();
		_of._fn					= _fname;
		_of._used				= 0;
		return					_of;
	}
	return *it;
}

void setup_reader(CStreamReader* _r, _open_file& _of)
{
	_of._stream_reader		= _r;
}

void setup_reader(IReader* _r, _open_file& _of)
{
	_of._reader				= _r;
}

template <typename T>
void _register_open_file(T* _r, LPCSTR _fname)
{
	xrCriticalSection		_lock;
	_lock.Enter				();

	shared_str f			= _fname;
	_check_open_file		(f);
	
	_open_file& _of			= find_free_item(_fname);
	setup_reader			(_r,_of);
	_of._used				+= 1;

	_lock.Leave				();
}

template <typename T>
void _unregister_open_file(T* _r)
{
	xrCriticalSection		_lock;
	_lock.Enter				();

	xr_vector<_open_file>::iterator it	= std::find_if(g_open_files.begin(), g_open_files.end(), eq_pointer<T>(_r) );
	VERIFY								(it!=g_open_files.end());
	_open_file&	_of						= *it;
	_of._reader							= NULL;
	_lock.Leave				();
}

XRCORE_API void _dump_open_files(int mode)
{
	xr_vector<_open_file>::iterator it		= g_open_files.begin();
	xr_vector<_open_file>::iterator it_e	= g_open_files.end();

	bool bShow = false;
	if(mode==1)
	{
		for(; it!=it_e; ++it)
		{
			_open_file& _of = *it;
			if(_of._reader!=NULL)
			{
				if(!bShow)
					LogInfo("%s", "----opened files");

				bShow = true;
				LogInfo("[%d] fname:%s", _of._used ,_of._fn.c_str());
			}
		}
	}else
	{
		LogInfo("%s", "----un-used");
		for(it = g_open_files.begin(); it!=it_e; ++it)
		{
			_open_file& _of = *it;
			if(_of._reader==NULL)
				LogInfo("[%d] fname:%s", _of._used ,_of._fn.c_str());
		}
	}
	if(bShow)
		LogInfo("%s", "----total count=",g_open_files.size());
}

CLocatorAPI::CLocatorAPI()
#ifdef PROFILE_CRITICAL_SECTIONS
	:m_auth_lock			(MUTEX_PROFILE_ID(CLocatorAPI::m_auth_lock))
#endif // PROFILE_CRITICAL_SECTIONS
{
    m_Flags.zero		();
	// get page size
	SYSTEM_INFO			sys_inf;
	GetSystemInfo		(&sys_inf);
	dwAllocGranularity	= sys_inf.dwAllocationGranularity;
    //m_iLockRescan		= 0; 
	dwOpenCounter		= 0;
}

CLocatorAPI::~CLocatorAPI()
{
	VERIFY				(0==m_iLockRescan);
	_dump_open_files	(1);
}

void CLocatorAPI::Register		(LPCSTR name, u32 vfs, u32 crc, u32 ptr, u32 size_real, u32 size_compressed, u32 modif)
{
	ZoneScoped;

	//LogInfo("Register[%d] [%s]",vfs,name);
	string512			temp_file_name;
	xr_strcpy			(temp_file_name,sizeof(temp_file_name),name);
	xr_strlwr			(temp_file_name);

	// Register file
	file				desc;
	desc.name			= temp_file_name;
	desc.vfs			= vfs;
	desc.crc			= crc;
	desc.ptr			= ptr;
	desc.size_real		= size_real;
	desc.size_compressed= size_compressed;
    desc.modif			= modif & (~u32(0x3));

	if (m_Flags.is(flLoadingAddons))
	{
		// a little bit of a hack

		desc.real_file_path = temp_file_name;

		xr_string s = temp_file_name;
		s.replace(0, addon_base_path.length(), target_base_path);

		strcpy_s(temp_file_name, s.c_str());
	}

	// if file already exist - update info
	if (const files_it I = file_find_it(temp_file_name); I != m_files.end())
	{
		desc.name = I->name;

		// sad but true, performance option
		// correct way is to erase and then insert new record:
		const_cast<file&>(*I) = desc;
		return;
	}

	desc.name = xr_strdup(temp_file_name);

	// otherwise insert file
	m_files.insert		(desc); 
	
	// Try to register folder(s)
	string_path			temp;	
	xr_strcpy			(temp,sizeof(temp),desc.name);
	string_path			path;
	string_path			folder;

	while (temp[0]) 
	{
		_splitpath		(temp, path, folder, 0, 0 );
        xr_strcat			(path,folder);

		if (!exist(path))	
		{
			desc.name			= xr_strdup(path);
			desc.vfs			= 0xffffffff;
			desc.ptr			= 0;
			desc.size_real		= 0;
			desc.size_compressed= 0;
            desc.modif			= u32(-1);
            std::pair<files_it,bool> I2 = m_files.insert(desc); 

            R_ASSERT(I2.second);
		}

		xr_strcpy					(temp,sizeof(temp),folder);

		if (xr_strlen(temp))
			temp[xr_strlen(temp)-1]=0;
	}
}

void CLocatorAPI::MountZDB()
{
	ZoneScoped;

	PathPairIt pArchIt		= pathes.find("$arch_dir$");
	PathPairIt pDataIt		= pathes.find("$game_data$");
	if (pArchIt == pathes.end() || pDataIt == pathes.end())
		return;

	// zdb archives live under the fs root: <root>\zdb\*.zdb
	string_path		zdb_dir;
	strconcat		(sizeof(zdb_dir), zdb_dir, pArchIt->second->m_Path, "zdb\\");

	// Virtual FS: gamedata is both the disk override root (real files win)
	// and the prefix to strip from absolute paths before archive lookup.
	xrFS&			vfs		= xrFS::instance();
	vfs.set_data_root		(pDataIt->second->m_Path);

	// VFS virtual root (no trailing separator): prefix stripped by vfs_resolve
	string_path		vroot;
	xr_strcpy		(vroot, sizeof(vroot), pDataIt->second->m_Path);
	{
		size_t len	= xr_strlen(vroot);
		if (len && vroot[len-1]=='\\')
			vroot[len-1] = 0;
	}
	xr_strlwr		(vroot);
	std::string		virtual_root = vroot;

	// Locator key prefix (with trailing separator, lowercased)
	string_path		game_data_root;
	xr_strcpy		(game_data_root, sizeof(game_data_root), pDataIt->second->m_Path);
	xr_strlwr		(game_data_root);
	{
		size_t len	= xr_strlen(game_data_root);
		if (!len || game_data_root[len-1] != '\\')
			xr_strcat(game_data_root, sizeof(game_data_root), "\\");
	}
	size_t			gd_len = xr_strlen(game_data_root);

	std::vector<std::string>	archives;
	std::error_code			ec;
	std::filesystem::recursive_directory_iterator dir_it(
		std::filesystem::path(zdb_dir),
		std::filesystem::directory_options::skip_permission_denied,
		ec), dir_end;
	for (; dir_it != dir_end; dir_it.increment(ec))
	{
		if (ec)
		{
			ec.clear();
			continue;
		}
		if (!dir_it->is_regular_file(ec))
			continue;
		if (ec)
		{
			ec.clear();
			continue;
		}
		const std::string	name = dir_it->path().filename().string();
		if (select_zdb_archive(name.c_str()))
			archives.push_back(dir_it->path().string());
	}
	std::sort(archives.begin(), archives.end());

	if (archives.empty())
	{
		LogInfo("VFS: no zdb archives in [%s]", zdb_dir);
		return;
	}

	// Mount all archives in parallel: every archive is mmap'd and its central
	// directory parsed across the worker pool (xrFS::mount_zdb_many), then the
	// merged flat index is registered into the locator in one pass.
	const size_t mounted = vfs.mount_zdb_many(archives, virtual_root);
	if (mounted == 0)
	{
		LogInfo("VFS: no zdb archives could be mounted from [%s]", zdb_dir);
		return;
	}

	u32				registered = 0;
	const std::vector<std::string> names = vfs.list_archive_files();
	for (const std::string& rel : names)
	{
		// locator key: gamedata root + relative entry name, '\' separators
		string_path	key;
		strconcat	(sizeof(key), key, game_data_root, rel.c_str());
		for (char* c = key + gd_len; *c; ++c)
			if (*c == '/') *c = '\\';

		// real files (and addons) virtually override the archive entries
		if (file_find_it(key) != m_files.end())
			continue;

		u32			size = 0, crc = 0, arc_time = 0;
		vfs.archive_meta(rel, size, crc, &arc_time);

		Register	(key, ZDB_VFS, crc, 0, size, size, arc_time);
		++registered;

		if (strstr(rel.c_str(), "prefetch/prefetch.ltx"))
			LogInfo("VFS-DBG registered key: [%s] size=%u", key, size);
	}

	LogInfo("VFS: mounted %d zdb archive(s), %d virtual file(s) registered", (int)mounted, registered);
}

bool CLocatorAPI::select_zdb_archive(LPCSTR filename) const
{
	LPCSTR ext = strrchr(filename, '.');
	if (!ext)
		return false;
	if (0 != stricmp(ext, ".zdb"))
		return false;
	return true;
}

void CLocatorAPI::prefetch_zdb(const xr_vector<shared_str>& names)
{
	if (names.empty())
		return;
	std::vector<std::string> paths;
	paths.reserve(names.size());
	for (const shared_str& n : names)
		paths.push_back(n.c_str());
	xrFS::instance().prefetch_virtual(paths);
}

void CLocatorAPI::ProcessOne(LPCSTR path, void* _F, bool bNoRecurse)
{
	ZoneScoped;

	_finddata_t& F	= *((_finddata_t*)_F);

	string_path		N;
	xr_strcpy		(N,sizeof(N),path);
	xr_strcat		(N,F.name);
	xr_strlwr		(N);
	
	if (F.attrib&_A_HIDDEN)			return;

	if (F.attrib&_A_SUBDIR) {
		//if (bNoRecurse)					return;
		if (0==xr_strcmp(F.name,"."))	return;
		if (0==xr_strcmp(F.name,".."))	return;
		xr_strcat		(N,"\\");

		if (bNoRecurse)
		{
			Register(N, 0xffffffff, 0, 0, 0, (u32)-1, true);
			return;
		}

		Register	(N,0xffffffff,0,0,F.size,F.size,(u32)F.time_write);
		Recurse		(N, bNoRecurse);
	} else {
		Register		(N,0xffffffff,0,0,F.size,F.size,(u32)F.time_write);
	}
}

IC bool pred_str_ff(const _finddata_t& x, const _finddata_t& y)
{	
	return xr_strcmp(x.name,y.name)<0;	
}


bool ignore_name(const char* _name)
{
	// ignore windows hidden Thumbs.db
	if (0 == strcmp(_name, "Thumbs.db"))
		return true;

	// ignore processing ".svn" folders
	return ( _name[0]=='.' && _name[1]=='s' && _name[2]=='v' && _name[3]=='n' && _name[4]==0);
}

// we need to check for file existance
// because Unicode file names can 
// be interpolated by FindNextFile()

bool ignore_path(const char* _path){
	HANDLE h = CreateFile( _path, 0, 0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_READONLY | FILE_FLAG_NO_BUFFERING, NULL);

	if (h!=INVALID_HANDLE_VALUE)
	{
		CloseHandle(h);
		return false;
	}
	else
		return true;
}

bool CLocatorAPI::Recurse(LPCSTR path, bool bNoRecurse)
{
	ZoneScoped;

    _finddata_t		sFile;
    intptr_t		hFile;

	string_path		N;
	xr_strcpy		(N,sizeof(N),path);
	xr_strcat			(N,"*.*");

	rec_files.reserve(1224);

	// find all files    
	if (-1==(hFile=_findfirst(N, &sFile)))
	{
    	// LogInfo("%s", "! Wrong path: ",path);
    	return		false;
    }

	string1024 full_path;
	if (m_Flags.test(flNeedExistsCheck))
	{
		xr_strcpy(full_path,sizeof(full_path), path);
		xr_strcat(full_path, sFile.name);

		// �������� � ������ ��� ���� *.db* ��������� � ������������� �������
		if(!ignore_name(sFile.name) && !ignore_path(full_path))
			rec_files.push_back(sFile);

		while ( _findnext( hFile, &sFile ) == 0 )
		{
			xr_strcpy(full_path,sizeof(full_path), path);
			xr_strcat(full_path, sFile.name);
			if(!ignore_name(sFile.name) && !ignore_path(full_path)) 
				rec_files.push_back(sFile);
		}
	}
	else
	{
		// �������� � ������ ��� ���� *.db* ��������� � ������������� �������
		if(!ignore_name(sFile.name))
			rec_files.push_back(sFile);

		while ( _findnext( hFile, &sFile ) == 0 )
		{
			if(!ignore_name(sFile.name)) 
				rec_files.push_back(sFile);
		}

	}

	_findclose		( hFile );

	FFVec buffer(rec_files);
	rec_files.clear_not_free();
	std::sort		(buffer.begin(), buffer.end(), pred_str_ff);
	for (FFIt I = buffer.begin(), E = buffer.end(); I != E; ++I)
		ProcessOne	(path, &*I, bNoRecurse);

	// insert self
    if (path&&path[0])\
		Register	(path,0xffffffff,0,0,0,0,0);

    return true;
}

bool file_handle_internal	(LPCSTR file_name, u32 &size, int &file_handle);
void *FileDownload			(LPCSTR file_name, const int &file_handle, u32 &file_size);

void CLocatorAPI::setup_fs_path		(LPCSTR fs_name, string_path &fs_path)
{
	xr_strcpy			(fs_path,fs_name ? fs_name : "");
	LPSTR				slash = strrchr(fs_path,'\\');
	if (!slash)
		slash			= strrchr(fs_path,'/');
	if (!slash) {
		xr_strcpy		(fs_path,"");
		return;
	}

	*(slash+1)			= 0;
}

void CLocatorAPI::setup_fs_path		(LPCSTR fs_name)
{
	string_path			fs_path;
	setup_fs_path		(fs_name, fs_path);


	string_path			full_current_directory;
	_fullpath			(full_current_directory, fs_path, sizeof(full_current_directory));

	FS_Path				*path = xr_new<FS_Path>(full_current_directory,"","","",0);
#ifdef DEBUG
	LogInfo("$fs_root$ = %s", full_current_directory);
#endif // #ifdef DEBUG

	pathes.insert		(
		std::make_pair(
			xr_strdup("$fs_root$"),
			path
		)
	);
}

IReader *CLocatorAPI::setup_fs_ltx	(LPCSTR fs_name)
{
	setup_fs_path	(fs_name);

//	if (m_Flags.is(flTargetFolderOnly)) {
//		append_path	("$fs_root$", "", 0, FALSE);
//		return		(0);
//	}

	LPCSTR			fs_file_name = FSLTX;
	if (fs_name && *fs_name)
		fs_file_name= fs_name;
				
	LogInfo("using fs-ltx",fs_file_name);

	int				file_handle;
	u32				file_size;
	IReader			*result = 0;
	CHECK_OR_EXIT	(
		file_handle_internal(fs_file_name, file_size, file_handle),
		make_string("Cannot open file \"%s\".\nCheck your working folder.",fs_file_name)
	);

	void			*buffer = FileDownload(fs_file_name, file_handle, file_size);
	result			= xr_new<CTempReader>(buffer,file_size,0);

#ifdef DEBUG
	if (result && m_Flags.is(flBuildCopy|flReady))
		copy_file_to_build	(result, fs_file_name);
#endif // DEBUG

	if (m_Flags.test(flDumpFileActivity))
		_register_open_file	(result, fs_file_name);

	return			(result);
}

void CLocatorAPI::_initialize	(u32 flags, LPCSTR target_folder, LPCSTR fs_name)
{
	ZoneScoped;

	char _delimiter = '|'; //','
	if (m_Flags.is(flReady))return;
	CTimer t;
	t.Start();
	LogInfo("%s", "Initializing File System...");
	u32	M1			= Memory.mem_usage();

	m_Flags.set		(flags,TRUE);

	// scan root directory
	bNoRecurse		= TRUE;
	string4096		buf;

	// append application path
	if (m_Flags.is(flScanAppRoot))
		append_path	("$app_root$",Core.ApplicationPath,0,FALSE);


	//-----------------------------------------------------------
	// append application data path
	// target folder 
	if (m_Flags.is(flTargetFolderOnly))
	{
		append_path		("$target_folder$",target_folder,0,TRUE);
	}
	else
	{
		ZoneScopedN("Process FS ltx");
		IReader			*pFSltx = setup_fs_ltx(fs_name);
/*
		LPCSTR fs_ltx	= (fs_name&&fs_name[0])?fs_name:FSLTX;
		F				= r_open(fs_ltx); 
		if (!F&&m_Flags.is(flScanAppRoot))
			F			= r_open("$app_root$",fs_ltx); 

		if (!F)
		{
			string_path tmpAppPath = "";
			xr_strcpy(tmpAppPath,sizeof(tmpAppPath), Core.ApplicationPath);
			if (xr_strlen(tmpAppPath))
			{
				tmpAppPath[xr_strlen(tmpAppPath)-1] = 0;
				if (strrchr(tmpAppPath, '\\'))
					*(strrchr(tmpAppPath, '\\')+1) = 0;

				FS_Path* pFSRoot		= FS.get_path("$fs_root$");
				pFSRoot->_set_root		(tmpAppPath);
				rescan_path				(pFSRoot->m_Path, pFSRoot->m_Flags.is(FS_Path::flRecurse));				
			}
			F				= r_open("$fs_root$",fs_ltx); 
		}

		LogInfo("%s", "using fs-ltx",fs_ltx);
*/

		// append all pathes    
		string_path		id, root, add, def, capt;
		LPCSTR			lp_add, lp_def, lp_capt;
		string16		b_v;
		string4096		temp;

		while(!pFSltx->eof())
		{
			ZoneScopedN("Read string");

			pFSltx->r_string			(buf,sizeof(buf));
			if(buf[0]==';')		continue;

			_GetItem			(buf,0,id,'=');

			if (!m_Flags.is(flBuildCopy)&&(0==xr_strcmp(id,"$build_copy$"))) 
				continue;

			_GetItem			(buf,1,temp,'=');
			int cnt				= _GetItemCount(temp,_delimiter);  
			R_ASSERT2			(cnt>=3,temp);
			u32 fl				= 0;
			_GetItem			(temp,0,b_v,_delimiter);	
			
			if (CInifile::IsBOOL(b_v)) 
				fl				|= FS_Path::flRecurse;

			_GetItem			(temp,1,b_v,_delimiter);	
			if (CInifile::IsBOOL(b_v)) 
				fl				|= FS_Path::flNotif;

			_GetItem			(temp,2,root,_delimiter);
			_GetItem			(temp,3,add,_delimiter);
			_GetItem			(temp,4,def,_delimiter);
			_GetItem			(temp,5,capt,_delimiter);
			xr_strlwr			(id);			
			

			xr_strlwr			(root);
			lp_add				=(cnt>=4)?xr_strlwr(add):0;
			lp_def				=(cnt>=5)?def:0;
			lp_capt				=(cnt>=6)?capt:0;
			
			PathPairIt p_it		= pathes.find(root);

			std::pair<PathPairIt, bool> I;
			FS_Path* P			= xr_new<FS_Path>((p_it!=pathes.end())?p_it->second->m_Path:root,lp_add,lp_def,lp_capt,fl);
			bNoRecurse			= !(fl&FS_Path::flRecurse);
			Recurse				(P->m_Path, bNoRecurse);
			I					= pathes.insert(std::make_pair(xr_strdup(id),P));
#ifndef DEBUG
			m_Flags.set			(flCacheFiles,FALSE);
#endif // DEBUG

			CHECK_OR_EXIT		(I.second,"The file 'fsgame.ltx' is corrupted (it contains duplicated lines).\nPlease reinstall the game or fix the problem manually.");
		}
		r_close			(pFSltx);
		R_ASSERT		(path_exist("$app_data_root$"));
	};

	string_path base_path;
	strconcat(sizeof(base_path), base_path, FS.get_path("$fs_root$")->m_Path, "gamedata");
	ProcessExternalAddons(base_path);

	MountZDB();

	u32	M2			= Memory.mem_usage();
	LogInfo("FS: %d files cached, %dKb memory used.",m_files.size(), (M2-M1)/1024);

	m_Flags.set		(flReady,TRUE);

	LogInfo("Init FileSystem %f sec",t.GetElapsed_sec());
	//-----------------------------------------------------------
	if (strstr(Core.Params, "-overlaypath"))
	{
		string1024				c_newAppPathRoot;
		sscanf					(strstr(Core.Params,"-overlaypath ")+13,"%[^ ] ",c_newAppPathRoot);
		FS_Path* pLogsPath = FS.get_path("$logs$");
		FS_Path* pAppdataPath = FS.get_path("$app_data_root$");


		if (pLogsPath) pLogsPath->_set_root(c_newAppPathRoot);
		if (pAppdataPath) 
		{
			pAppdataPath->_set_root(c_newAppPathRoot);
			rescan_path(pAppdataPath->m_Path, pAppdataPath->m_Flags.is(FS_Path::flRecurse));
		}
	}

	rec_files.clear	();
	//-----------------------------------------------------------

	xrAsyncLogger::instance().initialize_logs(0!=strstr(Core.Params,"-nolog"));
	xrDebug::OnFileSystemInitialized();
}

void CLocatorAPI::_destroy		()
{
	ZoneScoped;

	for (void* h : m_zdb_stream_maps)
		if (h) CloseHandle((HANDLE)h);
	m_zdb_stream_maps.clear();

	xrAsyncLogger::instance().stop();

	for				(files_it I=m_files.begin(); I!=m_files.end(); I++)
	{
		char* str	= LPSTR(I->name);
		xr_free		(str);
	}
	m_files.clear		();
	for				(PathPairIt p_it=pathes.begin(); p_it!=pathes.end(); p_it++)
    {
		char* str	= LPSTR(p_it->first);
		xr_free		(str);
		xr_delete	(p_it->second);
    }
	pathes.clear	();
}

const CLocatorAPI::file* CLocatorAPI::exist			(const char* fn)
{
	files_it it		= file_find_it(fn);
	return (it!=m_files.end())?&(*it):0;
}

const CLocatorAPI::file* CLocatorAPI::exist			(const char* path, const char* name)
{
	string_path		temp;       
    update_path		(temp,path,name);
	return			exist(temp);
}

const CLocatorAPI::file* CLocatorAPI::exist			(string_path& fn, LPCSTR path, LPCSTR name)
{
    update_path		(fn,path,name);
	return			exist(fn);
}

const CLocatorAPI::file* CLocatorAPI::exist			(string_path& fn, LPCSTR path, LPCSTR name, LPCSTR ext)
{
	string_path		nm;
	strconcat		(sizeof(nm),nm,name,ext);
    update_path		(fn,path,nm);
	return			exist(fn);
}

xr_vector<char*>* CLocatorAPI::file_list_open			(const char* initial, const char* folder, u32 flags)
{
	string_path		N;
	R_ASSERT		(initial&&initial[0]);
	update_path		(N,initial,folder);
	return			file_list_open(N,flags);
}

xr_vector<char*>* CLocatorAPI::file_list_open			(const char* _path, u32 flags)
{
	R_ASSERT		(_path);
	VERIFY			(flags);
	// ��������� ����� �� ��������������� ����
	//check_pathes	();

	string_path		N;

	if (path_exist(_path))	
		update_path	(N,_path,"");
	else					
		xr_strcpy(N,sizeof(N), _path);

	file			desc;
	desc.name		= N;
	files_it	I 	= m_files.find(desc);
	if (I==m_files.end())	return 0;
	
	xr_vector<char*>*	dest	= xr_new<xr_vector<char*> > ();

	size_t base_len		= xr_strlen(N);
	for (++I; I!=m_files.end(); ++I)
	{
		const file& entry = *I;
		if (0!=strncmp(entry.name,N,base_len))	break;	// end of list
		const char* end_symbol = entry.name+xr_strlen(entry.name)-1;
		if ((*end_symbol) !='\\')	{
			// file
			if ((flags&FS_ListFiles) == 0)	continue;

			const char* entry_begin = entry.name+base_len;
			if ((flags&FS_RootOnly)&&strstr(entry_begin,"\\"))	continue;	// folder in folder
			dest->push_back			(xr_strdup(entry_begin));
            LPSTR fname 			= dest->back();
            if (flags&FS_ClampExt)	if (0!=strext(fname)) *strext(fname)=0;
		} else {
			// folder
			if ((flags&FS_ListFolders) == 0)continue;
			const char* entry_begin = entry.name+base_len;
			
			if ((flags&FS_RootOnly)&&(strstr(entry_begin,"\\")!=end_symbol))	continue;	// folder in folder
			
			dest->push_back	(xr_strdup(entry_begin));
		}
	}
	return dest;
}

void	CLocatorAPI::file_list_close	(xr_vector<char*>* &lst)
{
	if (lst) 
	{
		for (xr_vector<char*>::iterator I=lst->begin(); I!=lst->end(); I++)
			xr_free	(*I);
		xr_delete	(lst);
	}
}

int CLocatorAPI::file_list(FS_FileSet& dest, LPCSTR path, u32 flags, LPCSTR mask)
{
	R_ASSERT		(path);
	VERIFY			(flags);
	// ��������� ����� �� ��������������� ����
    //check_pathes	();
               
	string_path		N;
	if (path_exist(path))	
		update_path	(N,path,"");
    else			
		xr_strcpy(N,sizeof(N),path);

	file			desc;
	desc.name		= N;
	files_it	I 	= m_files.find(desc);
	if (I==m_files.end())	return 0;

	SStringVec 		masks;
	_SequenceToList	(masks,mask);
    BOOL b_mask 	= !masks.empty();

	size_t base_len	= xr_strlen(N);
	for (++I; I!=m_files.end(); ++I)
	{
		const file& entry = *I;
		if (0!=strncmp(entry.name,N,base_len))	break;	// end of list
		LPCSTR end_symbol = entry.name+xr_strlen(entry.name)-1;
		if ((*end_symbol) !='\\')	
		{
			// file
			if ((flags&FS_ListFiles) == 0)	continue;
			LPCSTR entry_begin 		= entry.name+base_len;
			if ((flags&FS_RootOnly)&&strstr(entry_begin,"\\"))	continue;	// folder in folder
			// check extension
			if (b_mask){
				bool bOK			= false;
				for (SStringVecIt it=masks.begin(); it!=masks.end(); it++)
				{
					if (PatternMatch(entry_begin,it->c_str()))
					{
						bOK=true; 
						break;
					}
				}
				if (!bOK)			continue;
			}

			xr_string fn;
			if (flags&FS_ClampExt)
				fn = EFS.ChangeFileExt(entry_begin, "");
			else
				fn = entry_begin;
			u32 fl = (entry.vfs != 0xffffffff ? FS_File::flVFS : 0);
			dest.insert(FS_File(fn, entry.size_real, entry.modif, fl));
		} else {
			// folder
			if ((flags&FS_ListFolders) == 0)	continue;
			LPCSTR entry_begin 		= entry.name+base_len;

			if ((flags&FS_RootOnly)&&(strstr(entry_begin,"\\")!=end_symbol))	continue;	// folder in folder
			u32 fl = FS_File::flSubDir|(entry.vfs?FS_File::flVFS:0);
			dest.insert(FS_File(entry_begin,entry.size_real,entry.modif,fl));
		}
	}
	return dest.size();
}

void CLocatorAPI::check_cached_files	(LPCSTR fname, const u32 &fname_size, const file &desc, LPCSTR &source_name)
{
	string_path		fname_copy;
	if (pathes.size() <= 1)
		return;
	
	if (!path_exist("$server_root$"))
		return;

	LPCSTR			path_base = get_path("$server_root$")->m_Path;
	u32				len_base = xr_strlen(path_base);
	LPCSTR			path_file = fname;
	u32				len_file = xr_strlen(path_file);
	if (len_file <= len_base)
		return;

	if ((len_base == 1) && (*path_base == '\\'))
		len_base	= 0;

	if (0!=memcmp(path_base,fname,len_base))
		return;

	BOOL		bCopy	= FALSE;

	string_path	fname_in_cache	;
	update_path	(fname_in_cache,"$cache$",path_file+len_base);
	files_it	fit	= file_find_it(fname_in_cache);
	if (fit!=m_files.end())	
	{
		// use
		const file&	fc	= *fit;
		if ((fc.size_real == desc.size_real)&&(fc.modif==desc.modif))	{
			// use
		} else {
			// copy & use
			LogInfo("copy: db[%X],cache[%X] - '%s', ",desc.modif,fc.modif,fname);
			bCopy		= TRUE;
		}
	} else {
		// copy & use
		bCopy	= TRUE;
	}

	// copy if need
	if (bCopy) {
		IReader		*_src;
		if (desc.size_real<256*1024)	_src = xr_new<CFileReader>			(fname);
		else							_src = xr_new<CVirtualFileReader>	(fname);
		IWriter*	_dst	= xr_new<CFileWriter>			(fname_in_cache,false);
		_dst->w				(_src->pointer(),_src->length());
		xr_delete			(_dst);
		xr_delete			(_src);
		set_file_age		(fname_in_cache,desc.modif);
		Register			(fname_in_cache,0xffffffff,0,0,desc.size_real,desc.size_real,desc.modif);
	}

	// Use
	source_name		= &fname_copy[0];
	xr_strcpy		(fname_copy,sizeof(fname_copy),fname);
	xr_strcpy		((LPSTR)fname,fname_size,fname_in_cache);
}

void CLocatorAPI::file_from_cache_impl	(IReader *&R, LPCSTR fname, const file &desc)
{
	if (desc.size_real<16*1024) {
		R						= xr_new<CFileReader>(fname);
		return;
	}

	R							= xr_new<CVirtualFileReader>(fname);
}

void CLocatorAPI::file_from_cache_impl	(CStreamReader *&R, LPCSTR fname, const file &desc)
{
	CFileStreamReader			*r = xr_new<CFileStreamReader>();
	r->construct				(fname,BIG_FILE_READER_WINDOW_SIZE);
	R							= r;
}

template <typename T>
void CLocatorAPI::file_from_cache	(T *&R, LPCSTR fname, const u32 &fname_size, const file &desc, LPCSTR &source_name)
{
#ifdef DEBUG
	if (m_Flags.is(flCacheFiles))
		check_cached_files		(fname,fname_size,desc,source_name);
#endif // DEBUG
	
	file_from_cache_impl		(R,fname,desc);
}

void CLocatorAPI::file_from_archive	(IReader *&R, LPCSTR fname, const file &desc)
{
	// zdb virtual FS entry: decompress on the fly via xrFS
	if (desc.vfs != ZDB_VFS)
	{
		R = 0;
		return;
	}

	std::vector<char> data;
	if (!xrFS::instance().read_virtual(fname, data))
	{
		R = 0;
		return;
	}
	u8* dest				= xr_alloc<u8>(data.size());
	memcpy					(dest, data.data(), data.size());
	R						= xr_new<CTempReader>(dest, (int)data.size(), 0);
}

void CLocatorAPI::file_from_archive	(CStreamReader *&R, LPCSTR fname, const file &desc)
{
	// zdb virtual FS entry: decompress fully, serve from a page-file backed
	// mapping (CStreamReader only unmaps views; handle closed in _destroy)
	if (desc.vfs != ZDB_VFS)
	{
		R = 0;
		return;
	}

	std::vector<char> data;
	if (!xrFS::instance().read_virtual(fname, data))
	{
		R = 0;
		return;
	}
	HANDLE hMap				= CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)data.size(), nullptr);
	R_ASSERT				(hMap);
	u8* pv					= (u8*)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, 0);
	R_ASSERT				(pv);
	memcpy					(pv, data.data(), data.size());
	UnmapViewOfFile			(pv);
	R						= xr_new<CStreamReader>();
	R->construct			(hMap, 0, (u32)data.size(), (u32)data.size(), BIG_FILE_READER_WINDOW_SIZE);
	m_zdb_stream_maps.push_back(reinterpret_cast<void*>(hMap));
}

void CLocatorAPI::copy_file_to_build	(IWriter *W, IReader *r)
{
    W->w				(r->pointer(),r->length());
}

void CLocatorAPI::copy_file_to_build	(IWriter *W, CStreamReader *r)
{
	u32					buffer_size = r->length();
	u8					*buffer = xr_alloc<u8>(buffer_size);
	r->r				(buffer,buffer_size);
    W->w				(buffer,buffer_size);
	xr_free				(buffer);
	r->seek				(0);
}

template <typename T>
void CLocatorAPI::copy_file_to_build	(T *&r, LPCSTR source_name)
{
	string_path	cpy_name;
	string_path	e_cpy_name;
	FS_Path* 	P; 
	//if (!(source_name==strstr(source_name,(P=get_path("$server_root$"))->m_Path)||
 //       source_name==strstr(source_name,(P=get_path("$server_data_root$"))->m_Path)))
	//	return;

	string_path				fs_root;
	update_path				(fs_root,"$fs_root$","");
	LPCSTR const position	= strstr(source_name, fs_root);
	if ( position == source_name )
		update_path			(cpy_name,"$build_copy$",source_name + xr_strlen(fs_root));
	else
		update_path			(cpy_name,"$build_copy$",source_name);

	IWriter* W = w_open		(cpy_name);
    if (!W) {
        LogInfo("%s", "!Can't build:",source_name);
		return;
	}

	copy_file_to_build	(W,r);
    w_close				(W);
    set_file_age(cpy_name,get_file_age(source_name));
    if (!m_Flags.is(flEBuildCopy))
		return;

    LPCSTR ext		= strext(cpy_name);
    if (!ext)
		return;

    IReader* R		= 0;
    if (0==xr_strcmp(ext,".dds")){
        P			= get_path("$game_textures$");               
        update_path	(e_cpy_name,"$textures$",source_name+xr_strlen(P->m_Path));
        // tga
        *strext		(e_cpy_name) = 0;
        xr_strcat		(e_cpy_name,".tga");
        r_close		(R=r_open(e_cpy_name));
        // thm
        *strext		(e_cpy_name) = 0;
        xr_strcat		(e_cpy_name,".thm");
        r_close		(R=r_open(e_cpy_name));
		return;
    }
	
	if (0==xr_strcmp(ext,".ogg")){
        P			= get_path("$game_sounds$");                               
        update_path	(e_cpy_name,"$sounds$",source_name+xr_strlen(P->m_Path));
        // wav
        *strext		(e_cpy_name) = 0;
        xr_strcat		(e_cpy_name,".wav");
        r_close		(R=r_open(e_cpy_name));
        // thm
        *strext		(e_cpy_name) = 0;
        xr_strcat		(e_cpy_name,".thm");
        r_close		(R=r_open(e_cpy_name));
		return;
    }
	
	if (0==xr_strcmp(ext,".object")){
        xr_strcpy		(e_cpy_name,sizeof(e_cpy_name),source_name);
        // object thm
        *strext		(e_cpy_name) = 0;
        xr_strcat		(e_cpy_name,".thm");
        R			= r_open(e_cpy_name);
        if (R)		r_close	(R);
    }
}

bool CLocatorAPI::check_for_file	(LPCSTR path, LPCSTR _fname, string_path& fname_result, const file *&desc)
{
	// ��������� ����� �� ��������������� ����
    //check_pathes			();

	// correct path
	strcpy_s				(fname_result, _fname);
	xr_strlwr				(fname_result);
	if (path&&path[0])
		update_path			(fname_result, path, _fname);

	// Search entry
	file					desc_f;
	desc_f.name				= fname_result;

	files_it				I = m_files.find(desc_f);
	if (I == m_files.end())
	{
		if (strstr(fname_result, "prefetch"))
			LogInfo("VFS-DBG lookup MISS: [%s] (vfs=%u)", fname_result, desc ? desc->vfs : u32(-1));
		return				(false);
	}

	++dwOpenCounter;
	desc					= &*I;
	return					(true);
}

template <typename T>
T *CLocatorAPI::r_open_impl	(LPCSTR path, LPCSTR _fname)
{
	T						*R = 0;
	string_path				fname;
	const file				*desc = 0;
	LPCSTR					source_name = &fname[0];

	if (!check_for_file(path,_fname,fname,desc))
		return				(0);

	// OK, analyse
	if (0xffffffff == desc->vfs)
	{
		LPCSTR actual_name = !desc->real_file_path.size() ? fname : desc->real_file_path.c_str();

		file_from_cache(R, actual_name, sizeof(actual_name), *desc, source_name);
	}
	else
		file_from_archive	(R,fname,*desc);

	R->set_age(desc->modif);

#ifdef DEBUG
	if (R && m_Flags.is(flBuildCopy|flReady))
		copy_file_to_build	(R,source_name);
#endif // DEBUG

	if (m_Flags.test(flDumpFileActivity))
		_register_open_file	(R,fname);

	return					(R);
}

CStreamReader* CLocatorAPI::rs_open	(LPCSTR path, LPCSTR _fname)
{
	return					(r_open_impl<CStreamReader>(path,_fname));
}

IReader *CLocatorAPI::r_open	(LPCSTR path, LPCSTR _fname)
{
	return					(r_open_impl<IReader>(path,_fname));
}

void	CLocatorAPI::r_close	(IReader* &fs)
{
	if( m_Flags.test(flDumpFileActivity) )
		_unregister_open_file	(fs);

	xr_delete					(fs);
}

void	CLocatorAPI::r_close	(CStreamReader* &fs)
{
	if( m_Flags.test(flDumpFileActivity) )
		_unregister_open_file	(fs);

	fs->close					();
}

IWriter* CLocatorAPI::w_open	(LPCSTR path, LPCSTR _fname)
{
	string_path	fname;
	xr_strcpy(fname,_fname);
	xr_strlwr(fname);//,".$");
	if (path&&path[0]) update_path(fname,path,fname);
    CFileWriter* W 	= xr_new<CFileWriter>(fname,false); 
#ifdef _EDITOR
	if (!W->valid()) xr_delete(W);
#endif    
	return W;
}

IWriter* CLocatorAPI::w_open_ex	(LPCSTR path, LPCSTR _fname)
{
	string_path	fname;
	xr_strcpy(fname,_fname);
	xr_strlwr(fname);//,".$");
	if (path&&path[0]) update_path(fname,path,fname);
	CFileWriter* W 	= xr_new<CFileWriter>(fname,true); 
#ifdef _EDITOR
	if (!W->valid()) xr_delete(W);
#endif    
	return W;
}

void	CLocatorAPI::w_close(IWriter* &S)
{
	if (S){
        R_ASSERT	(S->fName.size());
        string_path	fname;
        xr_strcpy	(fname,sizeof(fname),*S->fName);
		bool bReg	= S->valid();
		xr_delete	(S);

		if(bReg)
		{
			struct _stat st;
			_stat		(fname,&st);
			Register	(fname,0xffffffff,0,0,st.st_size,st.st_size,(u32)st.st_mtime);
		}
    }
}

CLocatorAPI::files_it CLocatorAPI::file_find_it(LPCSTR fname)
{
	// ��������� ����� �� ��������������� ����
    //check_pathes	();

	file			desc_f;
	string_path		file_name;
	VERIFY			(xr_strlen(fname)*sizeof(char) < sizeof(file_name));
	xr_strcpy		(file_name,sizeof(file_name),fname);
	desc_f.name		= file_name;
//	desc_f.name		= xr_strlwr(xr_strdup(fname));
    files_it I		= m_files.find(desc_f);
//	xr_free			(desc_f.name);
	return			(I);
}

BOOL CLocatorAPI::dir_delete(LPCSTR path,LPCSTR nm,BOOL remove_files)
{
	string_path	fpath;
	if (path&&path[0]) 	update_path(fpath,path,nm);
    else				xr_strcpy(fpath,sizeof(fpath),nm);

    files_set 	folders;
	files_it I;
	// remove files
    I					= file_find_it(fpath);
    if (I!=m_files.end()){
        size_t base_len			= xr_strlen(fpath);
        for (; I!=m_files.end(); ){
            files_it cur_item	= I;
            const file& entry 	= *cur_item;
            I					= cur_item; I++;
            if (0!=strncmp(entry.name,fpath,base_len))	break;	// end of list
			const char* end_symbol = entry.name+xr_strlen(entry.name)-1;
			if ((*end_symbol) !='\\'){
//		        const char* entry_begin = entry.name+base_len;
				if (!remove_files) return FALSE;
		    	unlink		(entry.name);
				m_files.erase	(cur_item);
	        }else{
            	folders.insert(entry);
            }
        }
    }
    // remove folders
    files_set::reverse_iterator r_it = folders.rbegin();
    for (;r_it!=folders.rend();r_it++){
	    const char* end_symbol = r_it->name+xr_strlen(r_it->name)-1;
    	if ((*end_symbol) =='\\'){
        	_rmdir		(r_it->name);
            m_files.erase	(*r_it);
        }
    }
    return TRUE;
}

void CLocatorAPI::file_delete(LPCSTR path, LPCSTR nm)
{
	string_path	fname;
	if (path&&path[0]) 	update_path(fname,path,nm);
    else				xr_strcpy(fname,sizeof(fname),nm);

    const files_it I	= file_find_it(fname);
    if (I!=m_files.end()){
	    // remove file
    	unlink			(I->name);
		char* str		= LPSTR(I->name);
		xr_free			(str);
	    m_files.erase		(I);
    }
}

void CLocatorAPI::file_copy(LPCSTR src, LPCSTR dest)
{
#ifdef PROTECT_CBT
	FATAL("This build does not provide for this operation.");
#else
	if (exist(src))
	{
        IReader* S		= r_open(src);
        
		if (S)
		{
            IWriter* D	= w_open(dest);

            if (D)
			{
                D->w	(S->pointer(),S->length());
                w_close	(D);
            }

            r_close		(S);
        }
	}
#endif
}

void CLocatorAPI::file_rename(LPCSTR src, LPCSTR dest, bool bOwerwrite)
{
	files_it	S		= file_find_it(src);
	if (S!=m_files.end()){
		files_it D		= file_find_it(dest);
		if (D!=m_files.end()){ 
	        if (!bOwerwrite) return;
            unlink		(D->name);
			char* str	= LPSTR(D->name);
			xr_free		(str);
			m_files.erase	(D);
        }

        file new_desc	= *S;
		// remove existing item
		char* str		= LPSTR(S->name);
		xr_free			(str);
		m_files.erase		(S);
		// insert updated item
        new_desc.name	= xr_strlwr(xr_strdup(dest));
		m_files.insert	(new_desc); 
        
        // physically rename file
        VerifyPath		(dest);
        rename			(src,dest);
	}
}

int	CLocatorAPI::file_length(LPCSTR src)
{
	files_it	I		= file_find_it(src);
	return (I!=m_files.end())?I->size_real:-1;
}

bool CLocatorAPI::path_exist(LPCSTR path)
{
    PathPairIt P 			= pathes.find(path); 
    return					(P!=pathes.end());
}

FS_Path* CLocatorAPI::append_path(LPCSTR path_alias, LPCSTR root, LPCSTR add, BOOL recursive)
{
	VERIFY			(root/**&&root[0]/**/);
	VERIFY			(false==path_exist(path_alias));
	FS_Path* P		= xr_new<FS_Path>(root,add,LPCSTR(0),LPCSTR(0),0);
	bNoRecurse		= !recursive;
	Recurse			(P->m_Path, bNoRecurse);
	pathes.insert	(std::make_pair(xr_strdup(path_alias),P));
	return P;
}

FS_Path* CLocatorAPI::get_path(LPCSTR path)
{
    PathPairIt P 			= pathes.find(path); 
    R_ASSERT2(P!=pathes.end(),path);
    return P->second;
}

static std::recursive_mutex file_copy_mutex;

LPCSTR CLocatorAPI::update_path(string_path& dest, LPCSTR initial, LPCSTR src)
{
	static bool dev_reference_copy = (strstr(Core.Params, "-mfs_nfsc") || strstr(Core.Params, "-mfs_nftc") || strstr(Core.Params, "-mfs_nfmc"));

	string_path src_origin{};

	if (dev_reference_copy)
	{
		strcpy_s(src_origin, sizeof(src_origin), src);
	}

	LPCSTR r = get_path(initial)->_update(dest, src);

	if (dev_reference_copy)
	{
		std::scoped_lock lock(file_copy_mutex);
		string_path fn_ref;

		if (strstr(Core.Params, "-mfs_nfsc") && strcmp(initial, "$game_sounds$") == 0)
		{
			if (!exist(dest))
			{
				if (exist(fn_ref, "$game_sounds_reference$", src_origin))
				{
					LogInfo("[NFSC]: Sound founded: [%s] in $game_sounds_reference$ folder. Copying begins...", src_origin);
					file_copy(fn_ref, dest);

					LogInfo("[NFSC]: Copy from reference sounds folder done: [%s]", src_origin);
				}
			}
		}
		else if (strstr(Core.Params, "-mfs_nftc") && strcmp(initial, "$game_textures$") == 0)
		{
			if (!exist(dest))
			{
				if (exist(fn_ref, "$game_textures_reference$", src_origin))
				{
					LogInfo("[NFTC]: Texture founded: [%s] in $game_textures_reference$ folder. Copying begins...", src_origin);
					file_copy(fn_ref, dest);

					LogInfo("[NFTC]: Copy from reference textures folder done: [%s]", src_origin);
				}
			}
		}
		else if (strstr(Core.Params, "-mfs_nfmc") && strcmp(initial, "$game_meshes$") == 0)
		{
			if (!exist(dest))
			{
				if (exist(fn_ref, "$game_meshes_reference$", src_origin))
				{
					LogInfo("[NFMC]: Mesh founded: [%s] in $game_meshes_reference$ folder. Copying begins...", src_origin);
					file_copy(fn_ref, dest);

					LogInfo("[NFMC]: Copy from reference meshes folder done: [%s]", src_origin);
				}
			}
		}
	}

	return r;
}

/*
void CLocatorAPI::update_path(xr_string& dest, LPCSTR initial, LPCSTR src)
{
    return get_path(initial)->_update(dest,src);
}*/

u32 CLocatorAPI::get_file_age(LPCSTR nm)
{
	// ��������� ����� �� ��������������� ����
    //check_pathes	();

	files_it I 		= file_find_it(nm);
    return (I!=m_files.end())?I->modif:u32(-1);
}

void CLocatorAPI::set_file_age(LPCSTR nm, u32 age)
{
	// ��������� ����� �� ��������������� ����
    //check_pathes	();

    // set file
    _utimbuf	tm;
    tm.actime	= age;
    tm.modtime	= age;
    int res 	= _utime(nm,&tm);
    if (0!=res){
    	LogInfo("!Can't set file age: '%s'. Error: '%s'",nm,_sys_errlist[errno]);
    }else{
        // update record
        files_it I 		= file_find_it(nm);
        if (I!=m_files.end()){
            file& F		= (file&)*I;
            F.modif		= age;
        }
    }
}

void CLocatorAPI::rescan_path(LPCSTR full_path, BOOL bRecurse)
{
	file desc; 
    desc.name		= full_path;
	files_it	I 	= m_files.lower_bound(desc);
	if (I==m_files.end())	return;
	
	size_t base_len			= xr_strlen(full_path);
	for (; I!=m_files.end(); ){
    	files_it cur_item	= I;
		const file& entry 	= *cur_item;
    	I					= cur_item; I++;
		if (0!=strncmp(entry.name,full_path,base_len))	break;	// end of list
		if (entry.vfs!=0xFFFFFFFF)						continue;
		const char* entry_begin = entry.name+base_len;
        if (!bRecurse&&strstr(entry_begin,"\\"))		continue;
        // erase item
		char* str		= LPSTR(cur_item->name);
		xr_free			(str);
		m_files.erase		(cur_item);
	}
    bNoRecurse	= !bRecurse;
    Recurse		(full_path, bNoRecurse);
}

void  CLocatorAPI::rescan_pathes()
{
	//m_Flags.set(flNeedRescan,FALSE);
	for (PathPairIt p_it=pathes.begin(); p_it!=pathes.end(); p_it++)
    {
    	FS_Path* P	= p_it->second;
        if (P->m_Flags.is(FS_Path::flNeedRescan)){
			rescan_path(P->m_Path,P->m_Flags.is(FS_Path::flRecurse));
			P->m_Flags.set(FS_Path::flNeedRescan,FALSE);
        }
    }
}

/*void CLocatorAPI::lock_rescan()
{
	m_iLockRescan++;
}

void CLocatorAPI::unlock_rescan()
{
	m_iLockRescan--;  VERIFY(m_iLockRescan>=0);
	if ((0==m_iLockRescan)&&m_Flags.is(flNeedRescan)) 
		rescan_pathes();
}

void CLocatorAPI::check_pathes()
{
	if (m_Flags.is(flNeedRescan)&&(0==m_iLockRescan)){
    	lock_rescan		();
    	rescan_pathes	();
    	unlock_rescan	();
    }
}*/

BOOL CLocatorAPI::can_write_to_folder(LPCSTR path)
{
	if (path&&path[0]){
		string_path		temp;       
        LPCSTR fn		= "$!#%TEMP%#!$.$$$";
	    strconcat		(sizeof(temp),temp,path,path[xr_strlen(path)-1]!='\\'?"\\":"",fn);
		FILE* hf		= fopen	(temp, "wb");
		if (hf==0)		return FALSE;
        else{
        	fclose 		(hf);
	    	unlink		(temp);
            return 		TRUE;
        }
    }else{
    	return 			FALSE;
    }
}

BOOL CLocatorAPI::can_write_to_alias(LPCSTR path)
{
	string_path			temp;       
    update_path			(temp,path,"");
	return can_write_to_folder(temp);
}

BOOL CLocatorAPI::can_modify_file(LPCSTR fname)
{
	FILE* hf			= fopen	(fname, "r+b");
    if (hf){	
    	fclose			(hf);
        return 			TRUE;
    }else{
    	return 			FALSE;
    }
}

BOOL CLocatorAPI::can_modify_file(LPCSTR path, LPCSTR name)
{
	string_path			temp;       
    update_path			(temp,path,name);
	return can_modify_file(temp);
}

void CLocatorAPI::ProcessExternalAddons(LPCSTR base_path)
{
	// bool log_content = !strstr(Core.Params, "-dev");

	FS_FileSet fset;

	file_list(fset, "$mod_dir$", FS_ListFolders, "*.*");

	FS_FileSetIt it = fset.begin();
	FS_FileSetIt it_e = fset.end();

	string_path full_addon_path;

	LogInfo("base_path: [%s]", base_path);

	target_base_path = base_path;
	target_base_path += "\\";

	m_Flags.set(flLoadingAddons, TRUE);

	for (; it != it_e; ++it)
	{
		LogInfo("[AddonsMagager]: Found external addon: [%s], size: [%u]", it->name.c_str(), it->size);
		update_path(full_addon_path, "$mod_dir$", it->name.c_str());

		LogInfo("full_addon_path: [%s]", full_addon_path);

		addon_base_path = full_addon_path;

		Recurse(full_addon_path, false);
	}

	m_Flags.set(flLoadingAddons, FALSE);

	addon_base_path = "";
	target_base_path = "";
}