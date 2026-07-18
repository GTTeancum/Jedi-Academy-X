
#include "../game/q_shared.h"
#include "qcommon.h"
#include "files.h"
#include "../win32/win_file.h"
#if !defined(STEFX_ELITE_FORCE_MP)
#include "../zlib/zlib.h"
#endif


//#define GOB_PROFILE
//#define CUSTOM_MP_GOBS

static	cvar_t		*fs_openorder;

#if defined(STEFX_ELITE_FORCE_MP)
#define MAX_PAKFILES 1024
#define STEFX_ZIP_SEEK_SCRATCH_SIZE (8 * 1024)
static byte s_stefxZipSeekScratch[STEFX_ZIP_SEEK_SCRATCH_SIZE];
#endif


#if !defined(STEFX_ELITE_FORCE_MP)
// Zlib Tech Ref says decompression should use about 44kb.  I'll
// go with 64kb as a safety factor...
#define ZI_STACKSIZE (64*1024)

static char* zi_stackTop = NULL;
static char* zi_stackBase = NULL;



//GOB stuff
//===========================================================================

struct gi_handleTable
{
	wfhandle_t file;
	bool used;
};

static gi_handleTable *gi_handles = NULL;
static int gi_cacheHandle = 0;

static GOBFSHandle gi_open(GOBChar* name, GOBAccessType type)
{
	if (type != GOBACCESS_READ) return (GOBFSHandle)0xFFFFFFFF;

	int f;
	for (f = 0; f < MAX_FILE_HANDLES; ++f)
	{
		if (!gi_handles[f].used) break;
	}

	if (f == MAX_FILE_HANDLES) return (GOBFSHandle)0xFFFFFFFF;
	
#ifdef CUSTOM_MP_GOBS
	gi_handles[f].file = WF_Open(name, true, strstr(name, "assets_mp.gob") ? true : false);
#else
	gi_handles[f].file = WF_Open(name, true, strstr(name, "assets.gob") ? true : false);
#endif
	if (gi_handles[f].file < 0) return (GOBFSHandle)0xFFFFFFFF;
	gi_handles[f].used = true;

	return (GOBFSHandle)f;
}

static GOBBool gi_close(GOBFSHandle* handle)
{
	WF_Close(gi_handles[(int)*handle].file);
	gi_handles[(int)*handle].used = false;
	return GOB_TRUE;
}

static GOBInt32 gi_read(GOBFSHandle handle, GOBVoid* buffer, GOBInt32 size)
{
	return WF_Read(buffer, size, gi_handles[(int)handle].file);
}

static GOBInt32 gi_seek(GOBFSHandle handle, GOBInt32 offset, GOBSeekType type)
{
	int _type;
	switch (type) {
	case GOBSEEK_START: _type = SEEK_SET; break;
	case GOBSEEK_CURRENT: _type = SEEK_CUR; break;
	case GOBSEEK_END: _type = SEEK_END; break;
	default: assert(0); _type = SEEK_SET; break;
	}

	return WF_Seek(offset, _type, gi_handles[(int)handle].file);
}

static GOBVoid* gi_alloc(GOBUInt32 size)
{
	return Z_Malloc(size, TAG_FILESYS, qfalse, 32);
}

static GOBVoid gi_free(GOBVoid* ptr)
{
	Z_Free(ptr);
}

static GOBBool cache_open(GOBUInt32 size)
{
	for (gi_cacheHandle = 0; gi_cacheHandle < MAX_FILE_HANDLES; ++gi_cacheHandle)
	{
		if (!gi_handles[gi_cacheHandle].used) break;
	}

	if (gi_cacheHandle == MAX_FILE_HANDLES) return GOB_FALSE;
	
	gi_handles[gi_cacheHandle].file = WF_Open("z:\\jedi.swap", false, true);
	if (gi_handles[gi_cacheHandle].file < 0) return GOB_FALSE;

	if (!WF_Resize(size, gi_handles[gi_cacheHandle].file))
	{
		WF_Close(gi_handles[gi_cacheHandle].file);
		return GOB_FALSE;
	}

	gi_handles[gi_cacheHandle].used = true;

	return GOB_TRUE;
}

static GOBBool cache_close(GOBVoid)
{
	WF_Close(gi_handles[gi_cacheHandle].file);
	gi_handles[gi_cacheHandle].used = false;
	return GOB_TRUE;
}

static GOBInt32 cache_read(GOBVoid* buffer, GOBInt32 size)
{
	return WF_Read(buffer, size, gi_handles[gi_cacheHandle].file);
}

static GOBInt32 cache_write(GOBVoid* buffer, GOBInt32 size)
{
	return WF_Write(buffer, size, gi_handles[gi_cacheHandle].file);
}

static GOBInt32 cache_seek(GOBInt32 offset)
{
	return WF_Seek(offset, SEEK_SET, gi_handles[gi_cacheHandle].file);
}

static voidpf zi_alloc(voidpf opaque, uInt items, uInt size)
{
	voidpf ret = zi_stackTop;
	
	zi_stackTop += items * size;
	assert(zi_stackTop < zi_stackBase + ZI_STACKSIZE);

	return ret;
}

static void zi_free(voidpf opaque, voidpf address)
{
}

static GOBInt32 gi_decompress_zlib(GOBVoid* source, GOBUInt32 sourceLen, 
	GOBVoid* dest, GOBUInt32* destLen)
{
	// Copied and modified version of zlib's uncompress()...

	z_stream stream;
	int err;

	stream.next_in = (Bytef*)source;
	stream.avail_in = (uInt)sourceLen;

	stream.next_out = (Bytef*)dest;
	stream.avail_out = (uInt)*destLen;
	if ((uLong)stream.avail_out != *destLen) return Z_BUF_ERROR;

	stream.zalloc = zi_alloc;
	stream.zfree = zi_free;
	zi_stackTop = zi_stackBase;

	err = inflateInit(&stream);
	if (err != Z_OK) return err;

	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		inflateEnd(&stream);
		return err == Z_OK ? Z_BUF_ERROR : err;
	}
	*destLen = stream.total_out;

	err = inflateEnd(&stream);
	return err;
}

GOBInt32 gi_decompress_null(GOBVoid* source, GOBUInt32 sourceLen, 
	GOBVoid* dest, GOBUInt32* destLen)
{
	if (sourceLen > *destLen) return -1;
	*destLen = sourceLen;

	memcpy(dest, source, sourceLen);
	return 0;
}

#ifdef GOB_PROFILE
static GOBVoid gi_profileread(GOBUInt32 code)
{
	code = LittleLong(code);
	Sys_Log("gob-prof-mp.dat", &code, sizeof(code), true);
}
#endif

//===========================================================================

#endif // !STEFX_ELITE_FORCE_MP




static void FS_CheckUsed(fileHandle_t f)
{
	if (!fsh[f].used)
	{
		Com_Error( ERR_FATAL, "Filesystem call attempting to use invalid handle\n" );
	}
}


int FS_filelength( fileHandle_t f )
{
	FS_CheckInit();
	FS_CheckUsed(f);
	
#if defined(STEFX_ELITE_FORCE_MP)
	if (fsh[f].zipFile)
	{
		return fsh[f].fileSize;
	}
#endif

	if (fsh[f].gob)
	{
		GOBUInt32 cur, end, crap;
		GOBSeek(fsh[f].ghandle, 0, GOBSEEK_CURRENT, &cur);
		GOBSeek(fsh[f].ghandle, 0, GOBSEEK_END, &end);
		GOBSeek(fsh[f].ghandle, cur, GOBSEEK_START, &crap);
		
		return end;
	}
	else
	{
		int pos = WF_Tell(fsh[f].whandle);
		WF_Seek(0, SEEK_END, fsh[f].whandle);
		int end = WF_Tell(fsh[f].whandle);
		WF_Seek(pos, SEEK_SET, fsh[f].whandle);

		return end;
	}
}


void FS_FCloseFile( fileHandle_t f )
{
	FS_CheckInit();
	FS_CheckUsed(f);

#if defined(STEFX_ELITE_FORCE_MP)
	if (fsh[f].zipFile)
	{
		unzCloseCurrentFile(fsh[f].handleFiles.file.z);
		if (fsh[f].handleFiles.unique)
		{
			unzClose(fsh[f].handleFiles.file.z);
		}
		memset(&fsh[f], 0, sizeof(fsh[f]));
		return;
	}
#endif

	if (fsh[f].gob)
		GOBClose(fsh[f].ghandle);
	else
		WF_Close(fsh[f].whandle);

	memset(&fsh[f], 0, sizeof(fsh[f]));
}


fileHandle_t FS_FOpenFileWrite( const char *filename )
{
	FS_CheckInit();
	
	fileHandle_t f = FS_HandleForFile();

	char* osname = FS_BuildOSPath( filename );
	fsh[f].whandle = WF_Open(osname, false, false);
	if (fsh[f].whandle >= 0)
	{
		fsh[f].used = qtrue;
		fsh[f].gob = qfalse;
		fsh[f].zipFile = qfalse;
		return f;
	}

	return 0;
}


/*
===========
FS_FOpenFileRead

Finds the file in the search path.
Returns filesize and an open FILE pointer.
Used for streaming data out of either a
separate file or a ZIP file.
===========
*/

static int FS_FOpenFileReadOS( const char *filename, fileHandle_t f )
{
#if defined(STEFX_ELITE_FORCE_MP)
	static qboolean loggedLooseRead = qfalse;

	char* osname = FS_BuildOSPath( filename );
	fsh[f].whandle = WF_Open(osname, true, false);
	if (fsh[f].whandle >= 0)
	{
		int len;
		if ( !loggedLooseRead )
		{
			Com_Printf( "STEFX_HM: loose-file OS read path active, first='%s'\n", filename );
			loggedLooseRead = qtrue;
		}
		fsh[f].used = qtrue;
		fsh[f].gob = qfalse;
		fsh[f].zipFile = qfalse;
		Q_strncpyz(fsh[f].name, filename, sizeof(fsh[f].name));
		len = FS_filelength(f);
		fsh[f].fileSize = len;
		return len;
	}
#else
	if (Sys_GetFileCode(filename) != -1)
	{
		char* osname = FS_BuildOSPath( filename );
		fsh[f].whandle = WF_Open(osname, true, false);
		if (fsh[f].whandle >= 0)
		{
			fsh[f].used = qtrue;
			fsh[f].gob = qfalse;
			fsh[f].zipFile = qfalse;
			return FS_filelength(f);
		}
	}
#endif
	return -1;
}


/*
===================
FS_BuildGOBPath

Qpath may have either forward or backwards slashes
===================
*/
static char *FS_BuildGOBPath(const char *qpath )
{
	static char path[2][MAX_OSPATH];
	static int toggle;
	
	toggle ^= 1;		// flip-flop to allow two returns without clash

	if (qpath[0] == '\\' || qpath[0] == '/')
	{
		Com_sprintf( path[toggle], sizeof( path[0] ), ".%s", qpath );
	}
	else
	{
		Com_sprintf( path[toggle], sizeof( path[0] ), ".\\%s", qpath );
	}

//	FS_ReplaceSeparators( path[toggle], '\\' );
	FS_ReplaceSeparators( path[toggle] );
	
	return path[toggle];
}


static int FS_FOpenFileReadGOB( const char *filename, fileHandle_t f )
{
	char* gobname = FS_BuildGOBPath( filename );
	if (GOBOpen(gobname, &fsh[f].ghandle) == GOBERR_OK)
	{
		fsh[f].used = qtrue;
		fsh[f].gob = qtrue;
		fsh[f].zipFile = qfalse;
		Q_strncpyz(fsh[f].name, filename, sizeof(fsh[f].name));
		return FS_filelength(f);
	}
	return -1;
}

#if defined(STEFX_ELITE_FORCE_MP)
static long FS_HashFileName( const char *fname, int hashSize )
{
	int i;
	long hash;
	char letter;

	hash = 0;
	i = 0;
	while (fname[i] != '\0')
	{
		letter = tolower(fname[i]);
		if (letter == '.')
		{
			break;
		}
		if (letter == '\\' || letter == PATH_SEP)
		{
			letter = '/';
		}
		hash += (long)(letter) * (i + 119);
		i++;
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20));
	hash &= (hashSize - 1);
	return hash;
}

static pack_t *FS_LoadZipFile( char *zipfile )
{
	fileInPack_t	*buildBuffer;
	pack_t			*pack;
	unzFile			uf;
	int				err;
	unz_global_info gi;
	char			filename_inzip[MAX_ZPATH];
	unz_file_info	file_info;
	int				i, len;
	long			hash;
	int				fs_numHeaderLongs;
	int				*fs_headerLongs;
	char			*namePtr;

	fs_numHeaderLongs = 0;

	Com_Printf( "STEFX_HM: FS PK3 load begin '%s'\n", zipfile );
	uf = unzOpen(zipfile);
	if (!uf)
	{
		Com_Printf( "STEFX_HM: FS PK3 open failed '%s'\n", zipfile );
		return NULL;
	}
	err = unzGetGlobalInfo(uf, &gi);
	if (err != UNZ_OK)
	{
		Com_Printf( "STEFX_HM: FS PK3 global info failed '%s' err=%d\n", zipfile, err );
		unzClose(uf);
		return NULL;
	}

	fs_packFiles += gi.number_entry;

	len = 0;
	unzGoToFirstFile(uf);
	for (i = 0; i < gi.number_entry; i++)
	{
		err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
		if (err != UNZ_OK)
		{
			break;
		}
		if (file_info.size_filename > MAX_QPATH)
		{
			Com_Error(ERR_FATAL, "ERROR: filename length > MAX_QPATH ( strlen(%s) = %d) \n", filename_inzip, file_info.size_filename);
		}
		len += strlen(filename_inzip) + 1;
		unzGoToNextFile(uf);
	}

	buildBuffer = (fileInPack_t *)Z_Malloc(gi.number_entry * sizeof(fileInPack_t) + len, TAG_FILESYS, qtrue);
	namePtr = ((char *)buildBuffer) + gi.number_entry * sizeof(fileInPack_t);
	fs_headerLongs = (int*)Z_Malloc(gi.number_entry * sizeof(int), TAG_FILESYS, qtrue);

	for (i = 1; i <= MAX_FILEHASH_SIZE; i <<= 1)
	{
		if (i > gi.number_entry)
		{
			break;
		}
	}

	pack = (pack_t*)Z_Malloc(sizeof(pack_t) + i * sizeof(fileInPack_t *), TAG_FILESYS, qtrue);
	memset(pack, 0, sizeof(pack_t) + i * sizeof(fileInPack_t *));
	pack->hashSize = i;
	pack->hashTable = (fileInPack_t **)(((char *)pack) + sizeof(pack_t));
	Q_strncpyz(pack->pakFilename, zipfile, sizeof(pack->pakFilename));
	pack->handle = uf;
	pack->numfiles = gi.number_entry;

	unzGoToFirstFile(uf);
	for (i = 0; i < gi.number_entry; i++)
	{
		err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
		if (err != UNZ_OK)
		{
			break;
		}
		if (file_info.uncompressed_size > 0)
		{
			fs_headerLongs[fs_numHeaderLongs++] = LittleLong(file_info.crc);
		}
		Q_strlwr(filename_inzip);
		hash = FS_HashFileName(filename_inzip, pack->hashSize);
		buildBuffer[i].name = namePtr;
		strcpy(buildBuffer[i].name, filename_inzip);
		namePtr += strlen(filename_inzip) + 1;
		unzGetCurrentFileInfoPosition(uf, &buildBuffer[i].pos);
		buildBuffer[i].next = pack->hashTable[hash];
		pack->hashTable[hash] = &buildBuffer[i];
		unzGoToNextFile(uf);
	}

	pack->checksum = Com_BlockChecksum(fs_headerLongs, 4 * fs_numHeaderLongs);
	pack->checksum = LittleLong(pack->checksum);
	pack->pure_checksum = pack->checksum;
	Z_Free(fs_headerLongs);

	pack->buildBuffer = buildBuffer;
	Com_Printf( "STEFX_HM: FS loaded PK3 '%s' files=%d checksum=%d\n",
		zipfile, pack->numfiles, pack->checksum );
	return pack;
}

static qboolean FS_TryAddPK3( const char *path, const char *dir, const char *pakName )
{
	searchpath_t	*search;
	pack_t			*pak;
	char			*pakfile;
	wfhandle_t		testHandle;

	pakfile = FS_BuildOSPath(path, dir, pakName);
	testHandle = WF_Open(pakfile, true, false);
	if (testHandle < 0)
	{
		return qfalse;
	}
	WF_Close(testHandle);

	pak = FS_LoadZipFile(pakfile);
	if (!pak)
	{
		return qfalse;
	}
	Q_strncpyz(pak->pakGamename, dir, sizeof(pak->pakGamename));
	COM_StripExtension(pakName, pak->pakBasename);

	search = (searchpath_t*)Z_Malloc(sizeof(searchpath_t), TAG_FILESYS, qtrue);
	search->pack = pak;
	search->dir = 0;
	search->next = fs_searchpaths;
	fs_searchpaths = search;
	return qtrue;
}

static void FS_AddKnownPK3Files( const char *path, const char *dir )
{
	int i;
	int loaded;
	char pakName[32];

	loaded = 0;
	Com_Printf( "STEFX_HM: FS PK3 explicit startup probe begin\n" );

	for (i = 0; i <= 32; ++i)
	{
		Com_sprintf(pakName, sizeof(pakName), "pak%d.pk3", i);
		if (FS_TryAddPK3(path, dir, pakName))
		{
			++loaded;
		}
	}

	for (i = 0; i <= 32; ++i)
	{
		Com_sprintf(pakName, sizeof(pakName), "asset%d.pk3", i);
		if (FS_TryAddPK3(path, dir, pakName))
		{
			++loaded;
		}
	}

	if (FS_TryAddPK3(path, dir, "xbox1.pk3"))
	{
		++loaded;
	}

	Com_Printf( "STEFX_HM: FS PK3 explicit startup probe done loaded=%d\n", loaded );
}

static qboolean FS_PK3FileExists( const char *filename )
{
	searchpath_t	*search;
	pack_t			*pak;
	fileInPack_t	*pakFile;
	long			hash;

	for (search = fs_searchpaths; search; search = search->next)
	{
		if (!search->pack)
		{
			continue;
		}

		pak = search->pack;
		hash = FS_HashFileName(filename, pak->hashSize);
		pakFile = pak->hashTable[hash];
		while (pakFile)
		{
			if (!FS_FilenameCompare(pakFile->name, filename))
			{
				return qtrue;
			}
			pakFile = pakFile->next;
		}
	}

	return qfalse;
}

static int FS_FOpenFileReadPK3( const char *filename, fileHandle_t f, qboolean uniqueFILE )
{
	searchpath_t	*search;
	pack_t			*pak;
	fileInPack_t	*pakFile;
	long			hash;

	for (search = fs_searchpaths; search; search = search->next)
	{
		if (!search->pack)
		{
			continue;
		}

		pak = search->pack;
		hash = FS_HashFileName(filename, pak->hashSize);
		pakFile = pak->hashTable[hash];
		while (pakFile)
		{
			if (!FS_FilenameCompare(pakFile->name, filename))
			{
				qboolean uniqueHandle = qfalse;
				unzFile z = pak->handle;
				unz_s *zfi;
				ZIP_FILE *temp;
				int len;

				if (uniqueFILE)
				{
					z = unzReOpen(pak->pakFilename, pak->handle);
					uniqueHandle = z ? qtrue : qfalse;
					if (!z)
					{
						Com_Printf( "STEFX_HM: FS PK3 reopen failed file='%s' pk3='%s'; using shared package handle\n",
							filename, pak->pakFilename );
						z = pak->handle;
					}
				}

				if (!z)
				{
					Com_Printf( "STEFX_HM: FS PK3 open failed file='%s' pk3='%s'\n",
						filename, pak->pakFilename );
					return -1;
				}

				zfi = (unz_s *)z;
				temp = zfi->file;
				if (unzSetCurrentFileInfoPosition(pak->handle, pakFile->pos) != UNZ_OK)
				{
					if (uniqueHandle)
					{
						unzClose(z);
					}
					return -1;
				}
				Com_Memcpy( zfi, pak->handle, sizeof(unz_s) );
				zfi->file = temp;

				if (unzOpenCurrentFile(z) != UNZ_OK)
				{
					if (uniqueHandle)
					{
						unzClose(z);
					}
					return -1;
				}

				len = zfi->cur_file_info.uncompressed_size;
				fsh[f].handleFiles.file.z = z;
				fsh[f].handleFiles.unique = uniqueHandle;
				fsh[f].used = qtrue;
				fsh[f].gob = qfalse;
				fsh[f].zipFile = qtrue;
				fsh[f].fileSize = len;
				fsh[f].zipFilePos = pakFile->pos;
				pak->referenced |= FS_GENERAL_REF;
				Q_strncpyz(fsh[f].name, filename, sizeof(fsh[f].name));
				return len;
			}
			pakFile = pakFile->next;
		}
	}

	return -1;
}
#endif


/*
===========
FS_FOpenFileRead

Finds the file in the search path.
Returns filesize and an open FILE pointer.
Used for streaming data out of either a
separate file or a ZIP file.
===========
*/
int FS_FOpenFileRead( const char *filename, fileHandle_t *file, qboolean uniqueFILE )
{
	FS_CheckInit();
	
	if ( file == NULL ) {
		Com_Error( ERR_FATAL, "FS_FOpenFileRead: NULL 'file' parameter passed\n" );
	}

	if ( !filename ) {
		Com_Error( ERR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed\n" );
	}

	if ( filename[0] == '/' || filename[0] == '\\' ) {
		filename++;
	}

	if ( strstr( filename, ".." ) || strstr( filename, "::" ) ) {
		*file = 0;
		return -1;
	}

	*file = FS_HandleForFile();
	fsh[*file].handleFiles.unique = uniqueFILE;

	int len;
	
#if defined(STEFX_ELITE_FORCE_MP)
	len = FS_FOpenFileReadOS(filename, *file);
	if (len < 0)
	{
		len = FS_FOpenFileReadPK3(filename, *file, uniqueFILE);
	}
#else
	if (fs_openorder->integer == 0)
	{
		// Release mode -- read from GOB first
		len = FS_FOpenFileReadGOB(filename, *file);
		if (len < 0) len = FS_FOpenFileReadOS(filename, *file);
	}
	else
	{
		// Debug mode -- external files override GOB
		len = FS_FOpenFileReadOS(filename, *file);
		if (len < 0) len = FS_FOpenFileReadGOB(filename, *file);
	}
#endif

	if (len >= 0) return len;

	Com_DPrintf ("Can't find %s\n", filename);
	
	*file = 0;
	return -1;
}

qboolean FS_PK3PatchFileExists( const char *filename )
{
#if defined(STEFX_ELITE_FORCE_MP)
	if ( !filename || !filename[0] )
	{
		return qfalse;
	}

	return FS_PK3FileExists(filename);
#else
	fileHandle_t f;
	int len;

	if ( !filename || !filename[0] )
	{
		return qfalse;
	}

	len = FS_FOpenFileRead( filename, &f, qfalse );
	if ( len < 0 )
	{
		return qfalse;
	}

	FS_FCloseFile( f );
	return qtrue;
#endif
}


/*
=================
FS_Read

Properly handles partial reads
=================
*/
int FS_Read( void *buffer, int len, fileHandle_t f )
{
	FS_CheckInit();
	FS_CheckUsed(f);

	if ( !f )
	{
		return 0;
	}
	
	if ( f <= 0  || f >= MAX_FILE_HANDLES )
	{
		Com_Error( ERR_FATAL, "FS_Read: Invalid handle %d\n", f );
	}

#if defined(STEFX_ELITE_FORCE_MP)
	if (fsh[f].zipFile)
	{
		return unzReadCurrentFile(fsh[f].handleFiles.file.z, buffer, len);
	}
#endif

	if (fsh[f].gob)
	{
		GOBUInt32 size = GOBRead(buffer, len, fsh[f].ghandle);
		if (size == GOB_INVALID_SIZE)
		{
#if defined(FINAL_BUILD)
			extern "C" void XBLog_Write(const char *msg);
			XBLog_Write("JAMP: ERR_DiscFail from FS_Read GOB_INVALID_SIZE");
			extern void ERR_SetDiscFailReason(int);
			ERR_SetDiscFailReason(1);
			extern void ERR_DiscFail(bool);
			ERR_DiscFail(false);
#else
			extern void ERR_SetDiscFailReason(int);
			ERR_SetDiscFailReason(1);
			extern void ERR_DiscFail(bool);
			ERR_DiscFail(false);
#endif
		}
		return size;
	}
	else
	{
		return WF_Read(buffer, len, fsh[f].whandle);
	}
}

/*
	MP has FS_Read2 which is supposed to do some extra logic.
	We don't care, and just call FS_Read()
*/
int FS_Read2( void *buffer, int len, fileHandle_t f )
{
	return FS_Read(buffer, len, f);
}

/*
=================
FS_Write
=================
*/
int FS_Write( const void *buffer, int len, fileHandle_t f )
{
	FS_CheckInit();
	FS_CheckUsed(f);

	if ( !f )
	{
		return 0;
	}
	
	if ( f <= 0  || f >= MAX_FILE_HANDLES )
	{
		Com_Error( ERR_FATAL, "FS_Read: Invalid handle %d\n", f );
	}

#if defined(STEFX_ELITE_FORCE_MP)
	if (fsh[f].zipFile)
	{
		Com_Error( ERR_FATAL, "FS_Write: Cannot write to PK3 files %d\n", f );
	}
	else
#endif
	if (fsh[f].gob)
	{
		Com_Error( ERR_FATAL, "FS_Write: Cannot write to GOB files %d\n", f );
	}
	else
	{
		return WF_Write(buffer, len, fsh[f].whandle);
	}

	return 0;
}


/*
=================
FS_Seek

=================
*/
#if defined(STEFX_ELITE_FORCE_MP)
static qboolean FS_ResetZipCurrentFile(fileHandle_t f)
{
	unzCloseCurrentFile(fsh[f].handleFiles.file.z);

	if (unzSetCurrentFileInfoPosition(fsh[f].handleFiles.file.z, fsh[f].zipFilePos) != UNZ_OK)
	{
		Com_Printf( "STEFX_HM: FS PK3 seek reset position failed file='%s'\n", fsh[f].name );
		return qfalse;
	}

	if (unzOpenCurrentFile(fsh[f].handleFiles.file.z) != UNZ_OK)
	{
		Com_Printf( "STEFX_HM: FS PK3 seek reopen failed file='%s'\n", fsh[f].name );
		return qfalse;
	}

	return qtrue;
}

static int FS_DiscardZipBytes(fileHandle_t f, int bytesToDiscard)
{
	int skipped = 0;

	while (bytesToDiscard > 0)
	{
		const int block = bytesToDiscard > STEFX_ZIP_SEEK_SCRATCH_SIZE ? STEFX_ZIP_SEEK_SCRATCH_SIZE : bytesToDiscard;
		const int read = FS_Read(s_stefxZipSeekScratch, block, f);

		if (read <= 0)
		{
			Com_Printf( "STEFX_HM: FS PK3 seek discard failed file='%s' wanted=%d skipped=%d read=%d\n",
				fsh[f].name, bytesToDiscard, skipped, read );
			return -1;
		}

		skipped += read;
		bytesToDiscard -= read;
	}

	return skipped;
}

static int FS_SeekZipFile(fileHandle_t f, long offset, int origin)
{
	const int current = unztell(fsh[f].handleFiles.file.z);
	int target;

	if (origin == FS_SEEK_CUR && current < 0)
	{
		Com_Printf( "STEFX_HM: FS PK3 seek tell failed file='%s' offset=%ld\n", fsh[f].name, offset );
		return -1;
	}

	switch (origin)
	{
	case FS_SEEK_SET:
		target = offset;
		break;
	case FS_SEEK_CUR:
		target = current + offset;
		break;
	case FS_SEEK_END:
		target = fsh[f].fileSize + offset;
		break;
	default:
		Com_Error(ERR_FATAL, "Bad origin in FS_Seek\n");
		return -1;
	}

	if (target < 0 || target > fsh[f].fileSize)
	{
		Com_Printf( "STEFX_HM: FS PK3 seek out of range file='%s' offset=%ld origin=%d current=%d size=%d target=%d\n",
			fsh[f].name, offset, origin, current, fsh[f].fileSize, target );
		return -1;
	}

	if (target < current || origin == FS_SEEK_SET || origin == FS_SEEK_END)
	{
		if (!FS_ResetZipCurrentFile(f))
		{
			return -1;
		}

		if (target == 0)
		{
			return 0;
		}

		return FS_DiscardZipBytes(f, target) < 0 ? -1 : target;
	}

	if (target == current)
	{
		return current;
	}

	return FS_DiscardZipBytes(f, target - current) < 0 ? -1 : target;
}
#endif

int FS_Seek( fileHandle_t f, long offset, int origin )
{
	FS_CheckInit();
	FS_CheckUsed(f);

#if defined(STEFX_ELITE_FORCE_MP)
	if (fsh[f].zipFile)
	{
		return FS_SeekZipFile(f, offset, origin);
	}
#endif

	if (fsh[f].gob)
	{
		int _origin;
		switch( origin ) {
		case FS_SEEK_CUR: _origin = GOBSEEK_CURRENT; break;
		case FS_SEEK_END: _origin = GOBSEEK_END; break;
		case FS_SEEK_SET: _origin = GOBSEEK_START; break;
		default:
			_origin = GOBSEEK_CURRENT;
			Com_Error( ERR_FATAL, "Bad origin in FS_Seek\n" );
			break;
		}

	GOBUInt32 pos;
	GOBSeek(fsh[f].ghandle, offset, _origin, &pos);
	return pos;
	}
	else
	{
		int _origin;
		switch( origin ) {
		case FS_SEEK_CUR: _origin = SEEK_CUR; break;
		case FS_SEEK_END: _origin = SEEK_END; break;
		case FS_SEEK_SET: _origin = SEEK_SET; break;
		default:
			_origin = SEEK_CUR;
			Com_Error( ERR_FATAL, "Bad origin in FS_Seek\n" );
			break;
		}

		return WF_Seek(offset, _origin, fsh[f].whandle);
	}
}


/*
=================
FS_Access
=================
*/
qboolean FS_Access( const char *filename )
{
#if defined(STEFX_ELITE_FORCE_MP)
	fileHandle_t f;
	int len;

	FS_CheckInit();

	if ( !filename || !filename[0] )
	{
		return qfalse;
	}

	if ( FS_PK3FileExists( filename ) )
	{
		return qtrue;
	}

	f = FS_HandleForFile();
	len = FS_FOpenFileReadOS( filename, f );
	if ( len >= 0 )
	{
		FS_FCloseFile( f );
		return qtrue;
	}
	return qfalse;
#else
	GOBBool status;
	
	FS_CheckInit();

	char* gobname = FS_BuildGOBPath( filename );
	if (GOBAccess(gobname, &status) != GOBERR_OK || status != GOB_TRUE)
	{
#if defined(STEFX_ELITE_FORCE_MP)
		fileHandle_t f = FS_HandleForFile();
		int len = FS_FOpenFileReadOS( filename, f );
		if ( len >= 0 )
		{
			FS_FCloseFile( f );
			return qtrue;
		}
		return qfalse;
#else
		return Sys_GetFileCode( filename ) != -1;
#endif
	}

	return qtrue;
#endif
}


/*
======================================================================================

CONVENIENCE FUNCTIONS FOR ENTIRE FILES

======================================================================================
*/

#ifdef _JK2MP
int	FS_FileIsInPAK(const char *filename, int *pChecksum)
#else
int	FS_FileIsInPAK(const char *filename)
#endif
{
	FS_CheckInit();

	if ( !filename ) {
		Com_Error( ERR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed\n" );
	}

#if defined(STEFX_ELITE_FORCE_MP)
#ifdef _JK2MP
	if (pChecksum)
	{
		*pChecksum = 0;
	}
#endif
	return (FS_PK3FileExists(filename) || FS_Access(filename)) ? 1 : -1;
#else
	GOBBool exists;
	GOBAccess(const_cast<GOBChar*>(filename), &exists);

#ifdef _JK2MP
	*pChecksum = 0;
#endif

	return exists ? 1 : -1;
#endif
}

static bool sbLargeRead = false;

// Warn the filesystem that a large read is coming (GLM), so it can use
// TempAlloc to get space!
void FS_LargeRead( void )
{
	sbLargeRead = true;
}
void FS_CancelLargeRead( void )
{
	sbLargeRead = false;
}

extern void *BonePoolTempAlloc( unsigned long size );
extern void BonePoolTempFree( void *p );

#if defined(STEFX_ELITE_FORCE_MP)
static qboolean FS_STEFX_TraceShaderRead( const char *qpath )
{
	const char *extension;

	if ( !qpath )
	{
		return qfalse;
	}
	if ( !Q_stricmp( qpath, "scripts/_console_shader_list_" ) )
	{
		return qtrue;
	}
	if ( Q_stricmpn( qpath, "scripts/", 8 ) && Q_stricmpn( qpath, "shaders/", 8 ) )
	{
		return qfalse;
	}

	extension = strrchr( qpath, '.' );
	return extension && !Q_stricmp( extension, ".shader" );
}
#endif

/*
============
FS_ReadFile

Filename are relative to the quake search path
a null buffer will just return the file length without loading
============
*/
int FS_ReadFile( const char *qpath, void **buffer )
{
	FS_CheckInit();
	
	if ( !qpath || !qpath[0] ) {
		Com_Error( ERR_FATAL, "FS_ReadFile with empty name\n" );
	}

#if defined(STEFX_ELITE_FORCE_MP)
	const qboolean traceShaderRead = FS_STEFX_TraceShaderRead( qpath );
	if ( traceShaderRead )
	{
		Com_Printf( "STEFX_HM: shader FS_ReadFile begin path='%s' buffer=%d\n", qpath, buffer ? 1 : 0 );
	}
#endif

	// stop sounds from repeating
	S_ClearSoundBuffer();

#if defined(STEFX_ELITE_FORCE_MP)
	if ( traceShaderRead )
	{
		Com_Printf( "STEFX_HM: shader FS_ReadFile sound clear done path='%s'\n", qpath );
	}
#endif

	fileHandle_t h;
	int len = FS_FOpenFileRead( qpath, &h, qfalse );
#if defined(STEFX_ELITE_FORCE_MP)
	if ( traceShaderRead )
	{
		Com_Printf( "STEFX_HM: shader FS_ReadFile open done path='%s' len=%d handle=%d\n", qpath, len, h );
	}
#endif
	if ( h == 0 )
	{
		if ( buffer ) *buffer = NULL;
		return -1;
	}
	
	if ( !buffer )
	{
		FS_FCloseFile(h);
		return len;
	}

	byte *buf;
	// Try to TempAlloc if we've got the hint that this could fail:
	if( sbLargeRead )
		buf = (byte *)BonePoolTempAlloc( len+1 );

	// If that didn't work, or wasn't suggested:
	if( !sbLargeRead || !buf )
		buf = (byte*)Z_Malloc( len+1, TAG_TEMP_WORKSPACE, qfalse, 32);

#if defined(STEFX_ELITE_FORCE_MP)
	if ( traceShaderRead )
	{
		Com_Printf( "STEFX_HM: shader FS_ReadFile allocation done path='%s' bytes=%d ptr=%08X\n",
			qpath, len + 1, (unsigned int)buf );
	}
#endif

	buf[len]='\0';

//	Z_Label(buf, qpath);

	const int read = FS_Read(buf, len, h);

#if defined(STEFX_ELITE_FORCE_MP)
	if ( traceShaderRead )
	{
		Com_Printf( "STEFX_HM: shader FS_ReadFile read done path='%s' requested=%d read=%d\n",
			qpath, len, read );
	}
#endif

	// guarantee that it will have a trailing 0 for string operations
	buf[len] = 0;
	FS_FCloseFile( h );

#if defined(STEFX_ELITE_FORCE_MP)
	if ( traceShaderRead )
	{
		Com_Printf( "STEFX_HM: shader FS_ReadFile close done path='%s'\n", qpath );
	}
#endif

	*buffer = buf;
	return len;
}


/*
=============
FS_FreeFile
=============
*/
void FS_FreeFile( void *buffer )
{
	FS_CheckInit();

	if ( !buffer ) {
		Com_Error( ERR_FATAL, "FS_FreeFile( NULL )" );
	}

	// If this was read in with sbLargeRead true, then it might not be from
	// the zone!
	extern bool IsBonePoolPointer( void *p );
	if( IsBonePoolPointer( buffer ) )
		BonePoolTempFree( buffer );
	else
		Z_Free( buffer );
}


int	FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode )
{
	FS_CheckInit();

	if (mode != FS_READ)
	{
		Com_Error( ERR_FATAL, "FSH_FOpenFile: bad mode" );
		return -1;
	}
	
	return FS_FOpenFileRead( qpath, f, qtrue );
}


int	FS_FTell( fileHandle_t f )
{
	FS_CheckInit();
	FS_CheckUsed(f);

#if defined(STEFX_ELITE_FORCE_MP)
	if (fsh[f].zipFile)
	{
		return unztell(fsh[f].handleFiles.file.z);
	}
#endif

	if (fsh[f].gob)
	{
		GOBUInt32 pos;
		GOBSeek(fsh[f].ghandle, 0, GOBSEEK_CURRENT, &pos);
		return pos;
	}
	else
	{
		return WF_Tell(fsh[f].whandle);
	}
}

/*
================
FS_Startup
================
*/
void FS_Startup( const char *gameName )
{
	Com_Printf( "----- FS_Startup -----\n" );

	fs_openorder = Cvar_Get( "fs_openorder", "0", 0 );
	fs_debug = Cvar_Get( "fs_debug", "0", 0 );
	fs_copyfiles = Cvar_Get( "fs_copyfiles", "0", CVAR_INIT );
	fs_cdpath = Cvar_Get ("fs_cdpath", Sys_DefaultCDPath(), CVAR_INIT );	
	fs_basepath = Cvar_Get ("fs_basepath", Sys_DefaultBasePath(), CVAR_INIT );
	fs_gamedirvar = Cvar_Get ("fs_game", gameName, CVAR_INIT|CVAR_SERVERINFO );
	fs_restrict = Cvar_Get ("fs_restrict", "", CVAR_INIT );
	Q_strncpyz( fs_gamedir, fs_gamedirvar->string[0] ? fs_gamedirvar->string : gameName, sizeof( fs_gamedir ) );
	Com_Printf( "JAMP: FS_Startup basepath='%s' gamedir='%s' fs_game='%s'\n",
		fs_basepath->string, fs_gamedir, fs_gamedirvar->string );

#if defined(STEFX_ELITE_FORCE_MP)
	{
		searchpath_t *search;
		int f;

		for (f = 0; f < MAX_FILE_HANDLES; ++f)
		{
			memset(&fsh[f], 0, sizeof(fsh[f]));
		}

		search = (searchpath_t *)Z_Malloc(sizeof(searchpath_t), TAG_FILESYS, qtrue);
		search->dir = (directory_t*)Z_Malloc(sizeof(*search->dir), TAG_FILESYS, qtrue);
		search->pack = 0;
		Q_strncpyz(search->dir->path, fs_basepath->string, sizeof(search->dir->path));
		Q_strncpyz(search->dir->gamedir, fs_gamedir, sizeof(search->dir->gamedir));
		search->next = fs_searchpaths;
		fs_searchpaths = search;

		FS_AddKnownPK3Files(fs_basepath->string, fs_gamedir);
		Com_Printf( "STEFX_HM: FS_Startup using loose files plus xbox1.pk3; GOB disabled\n" );
		Com_Printf( "----------------------\n" );
		return;
	}
#else

	gi_handles = new gi_handleTable[MAX_FILE_HANDLES];
	for (int f = 0; f < MAX_FILE_HANDLES; ++f)
	{
		fsh[f].used = false;
		gi_handles[f].used = false;
	}

	zi_stackBase = (char*)Z_Malloc(ZI_STACKSIZE, TAG_FILESYS, qfalse);

	GOBMemoryFuncSet mem;
	mem.alloc = gi_alloc;
	mem.free = gi_free;
	
	GOBFileSysFuncSet file;
	file.close = gi_close;
	file.open = gi_open;
	file.read = gi_read;
	file.seek = gi_seek;
	file.write = NULL;
	
	GOBCacheFileFuncSet cache;
	cache.close = cache_close;
	cache.open = cache_open;
	cache.read = cache_read;
	cache.seek = cache_seek;
	cache.write = cache_write;

	GOBCodecFuncSet codec = {
		2, // codecs
		{
			{ // Codec 0 - zlib
				'z', GOB_INFINITE_RATIO, // tag, ratio (ratio is meaningless for decomp)
				NULL,
				gi_decompress_zlib,
			},
			{ // Codec 1 - null
				'0', GOB_INFINITE_RATIO, // tag, ratio (ratio is meaningless for decomp)
				NULL,
				gi_decompress_null,
			},
		}
	};

	if (
#ifdef _XBOX
		GOBInit(&mem, &file, &codec, &cache)
#else
		GOBInit(&mem, &file, &codec, NULL)
#endif
		!= GOBERR_OK)
	{
		Com_Error( ERR_FATAL, "Could not initialize GOB" );
	}

#ifdef CUSTOM_MP_GOBS
	char* archive = FS_BuildOSPath( "assets_mp" );
#else
	char* archive = FS_BuildOSPath( "assets" );
#endif
	if (GOBArchiveOpen(archive, GOBACCESS_READ, GOB_FALSE, GOB_TRUE) != GOBERR_OK)
	{
#if defined(FINAL_BUILD)
		extern "C" void XBLog_Write(const char *msg);
		XBLog_Write("JAMP: ERR_DiscFail from FS_Startup GOBArchiveOpen");
		extern void ERR_SetDiscFailReason(int);
		ERR_SetDiscFailReason(1);
		extern void ERR_DiscFail(bool);
		ERR_DiscFail(false);
#else
		//Com_Error( ERR_FATAL, "Could not initialize GOB" );
#if defined(STEFX_ELITE_FORCE_MP)
		Com_Printf( "STEFX_HM: FS_Startup archive '%s' missing; using loose-file fallback\n", archive );
#endif
		Cvar_Set("fs_openorder", "1");
#endif
	}
	
	GOBSetCacheSize(1);
	GOBSetReadBufferSize(32 * 1024);

#ifdef GOB_PROFILE
	GOBProfileFuncSet profile = {
		gi_profileread
	};
	GOBSetProfileFuncs(&profile);
	GOBStartProfile();
#endif
#endif

	Com_Printf( "----------------------\n" );
}

/*
============================

DIRECTORY SCANNING FUCNTIONS

============================
*/

#define	MAX_FOUND_FILES	0x1000

/*
==================
FS_AddFileToList
==================
*/
static int FS_AddFileToList( char *name, char *list[MAX_FOUND_FILES], int nfiles ) {
	int		i;

	if ( nfiles == MAX_FOUND_FILES - 1 ) {
		return nfiles;
	}
	for ( i = 0 ; i < nfiles ; i++ ) {
		if ( !stricmp( name, list[i] ) ) {
			return nfiles;		// allready in list
		}
	}
//	list[nfiles] = CopyString( name );
	list[nfiles] = (char *) Z_Malloc( strlen(name) + 1, TAG_LISTFILES, qfalse );
	strcpy(list[nfiles], name);
	nfiles++;

	return nfiles;
}

/*
===============
FS_ListFiles

Returns a uniqued list of files that match the given criteria
from all search paths
===============
*/
char **FS_ListFiles( const char *path, const char *extension, int *numfiles )
{
	char			*netpath;
	int				numSysFiles;
	char			**sysFiles;
	char			*name;
	int				nfiles = 0;
	char			**listCopy;
	char			*list[MAX_FOUND_FILES];
	int				i;

	FS_CheckInit();

	if ( !path ) {
		*numfiles = 0;
		return NULL;
	}

#if defined(STEFX_ELITE_FORCE_MP)
	const qboolean traceShaderList =
		( !Q_stricmp( path, "scripts" ) || !Q_stricmp( path, "shaders" ) ) &&
		extension && !Q_stricmp( extension, ".shader" );
	if ( traceShaderList )
	{
		Com_Printf( "STEFX_HM: shader FS_ListFiles begin path='%s' extension='%s'\n", path, extension );
	}
#endif

	// We don't do any fancy searchpath magic here, it's all in the meta-file
	// that Sys_ListFiles will return
	netpath = FS_BuildOSPath( path );
#ifdef _JK2MP
	sysFiles = Sys_ListFiles( netpath, extension, NULL, &numSysFiles, qfalse );
#else
	sysFiles = Sys_ListFiles( netpath, extension, &numSysFiles, qfalse );
#endif
#if defined(STEFX_ELITE_FORCE_MP)
	if ( traceShaderList )
	{
		Com_Printf( "STEFX_HM: shader FS_ListFiles system list done path='%s' os='%s' count=%d ptr=%08X\n",
			path, netpath, numSysFiles, (unsigned int)sysFiles );
	}
#endif
	for ( i = 0 ; i < numSysFiles ; i++ ) {
		// unique the match
		name = sysFiles[i];
		nfiles = FS_AddFileToList( name, list, nfiles );
	}
	Sys_FreeFileList( sysFiles );

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		#if defined(STEFX_ELITE_FORCE_MP)
		if ( traceShaderList )
		{
			Com_Printf( "STEFX_HM: shader FS_ListFiles done path='%s' count=0\n", path );
		}
		#endif
		return NULL;
	}

	listCopy = (char**)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ), TAG_LISTFILES, qfalse);
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

#if defined(STEFX_ELITE_FORCE_MP)
	if ( traceShaderList )
	{
		Com_Printf( "STEFX_HM: shader FS_ListFiles done path='%s' count=%d\n", path, nfiles );
	}
#endif

	return listCopy;
}


/*
=================
FS_FreeFileList
=================
*/
void FS_FreeFileList( char **filelist )
{
	int		i;

	FS_CheckInit();

	if ( !filelist ) {
		return;
	}

	for ( i = 0 ; filelist[i] ; i++ ) {
		Z_Free( filelist[i] );
	}

	Z_Free( filelist );
}

/*
===============
FS_AddFileToListBuf
===============
*/
static int FS_AddFileToListBuf( char *name, char *listbuf, int bufsize, int nfiles )
{
	char	*p;

	if ( nfiles == MAX_FOUND_FILES - 1 ) {
		return nfiles;
	}

	if (name[0] == '/' || name[0] == '\\') {
		name++;
	}

	p = listbuf;
	while ( *p ) {
		if ( !stricmp( name, p ) ) {
			return nfiles;		// already in list
		}
		p += strlen( p ) + 1;
	}

	if ( ( p + strlen( name ) + 2 - listbuf ) > bufsize ) {
		return nfiles;		// list is full
	}

	strcpy( p, name );
	p += strlen( p ) + 1;
	*p = 0;

	return nfiles + 1;
}

/*
================
FS_GetFileList

Returns a uniqued list of files that match the given criteria
from all search paths
================
*/
int	FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize )
{
	int				nfiles = 0;
	int				i;
	char			*netpath;
	int				numSysFiles;
	char			**sysFiles;
	char			*name;

	FS_CheckInit();

	if ( !path ) {
		return 0;
	}
	if ( !extension ) {
		extension = "";
	}

	// Prime the file list buffer
	listbuf[0] = '\0';
	netpath = FS_BuildOSPath( path );
#ifdef _JK2MP
	sysFiles = Sys_ListFiles( netpath, extension, NULL, &numSysFiles, qfalse );
#else
	sysFiles = Sys_ListFiles( netpath, extension, &numSysFiles, qfalse );
#endif
	for ( i = 0 ; i < numSysFiles ; i++ ) {
		// unique the match
		name = sysFiles[i];
		nfiles = FS_AddFileToListBuf( name, listbuf, bufsize, nfiles );
	}
	Sys_FreeFileList( sysFiles );

	return nfiles;
}

/*
=================
 Filesytem STUBS
=================
*/

qboolean FS_ConditionalRestart(int checksumFeed)
{
	return qfalse;
}

void FS_ClearPakReferences(int flags)
{
	return;
}

const char *FS_LoadedPakNames(void)
{
	static char info[BIG_INFO_STRING];
	searchpath_t *search;

	info[0] = '\0';
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (!search->pack)
		{
			continue;
		}
		if (info[0])
		{
			Q_strcat(info, sizeof(info), " ");
		}
		Q_strcat(info, sizeof(info), search->pack->pakBasename);
	}
	return info;
}

const char *FS_ReferencedPakNames(void)
{
	static char info[BIG_INFO_STRING];
	searchpath_t *search;

	info[0] = '\0';
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (!search->pack)
		{
			continue;
		}
		if (!(search->pack->referenced || Q_stricmpn(search->pack->pakGamename, BASEGAME, strlen(BASEGAME))))
		{
			continue;
		}
		if (info[0])
		{
			Q_strcat(info, sizeof(info), " ");
		}
		Q_strcat(info, sizeof(info), search->pack->pakGamename);
		Q_strcat(info, sizeof(info), "/");
		Q_strcat(info, sizeof(info), search->pack->pakBasename);
	}
	return info;
}

void FS_SetRestrictions(void)
{
	return;
}

#ifdef _JK2MP
void FS_Restart(int checksumFeed)
#else
void FS_Restart(void)
#endif
{
	return;
}

qboolean FS_FileExists(const char *file)
{
	fileHandle_t f;
	int len;
#if defined(STEFX_ELITE_FORCE_MP)
	static qboolean loggedExistsCheck = qfalse;
#endif

	if ( !file || !file[0] )
	{
		return qfalse;
	}

	len = FS_FOpenFileRead( file, &f, qfalse );
	if ( len < 0 )
	{
		return qfalse;
	}

	FS_FCloseFile( f );

#if defined(STEFX_ELITE_FORCE_MP)
	if ( !loggedExistsCheck )
	{
		Com_Printf( "STEFX_HM: FS_FileExists Xbox check active, first='%s'\n", file );
		loggedExistsCheck = qtrue;
	}
#endif

	return qtrue;
}

void FS_UpdateGamedir(void)
{
	return;
}

void FS_PureServerSetReferencedPaks(const char *pakSums, const char *pakNames)
{
	return;
}

void FS_PureServerSetLoadedPaks(const char *pakSums, const char *pakNames)
{
	return;
}

const char *FS_ReferencedPakChecksums(void)
{
	static char info[BIG_INFO_STRING];
	searchpath_t *search;

	info[0] = '\0';
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (search->pack && (search->pack->referenced || Q_stricmpn(search->pack->pakGamename, BASEGAME, strlen(BASEGAME))))
		{
			Q_strcat(info, sizeof(info), va("%i ", search->pack->checksum));
		}
	}
	return info;
}

const char *FS_LoadedPakChecksums(void)
{
	static char info[BIG_INFO_STRING];
	searchpath_t *search;

	info[0] = '\0';
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (search->pack)
		{
			Q_strcat(info, sizeof(info), va("%i ", search->pack->checksum));
		}
	}
	return info;
}

const char *FS_LoadedPakPureChecksums(void)
{
	static char info[BIG_INFO_STRING];
	searchpath_t *search;

	info[0] = '\0';
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (search->pack)
		{
			Q_strcat(info, sizeof(info), va("%i ", search->pack->pure_checksum));
		}
	}
	return info;
}

const char *FS_ReferencedPakPureChecksums(void)
{
	static char info[BIG_INFO_STRING];
	searchpath_t *search;
	int checksum;
	int numPaks;

	info[0] = '\0';
	checksum = fs_checksumFeed;
	numPaks = 0;
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (search->pack && (search->pack->referenced || Q_stricmpn(search->pack->pakGamename, BASEGAME, strlen(BASEGAME))))
		{
			Q_strcat(info, sizeof(info), va("%i ", search->pack->pure_checksum));
			checksum ^= search->pack->pure_checksum;
			numPaks++;
		}
	}
	checksum ^= numPaks;
	Q_strcat(info, sizeof(info), va("%i ", checksum));
	return info;
}
