
/*
 * UNPUBLISHED -- Rights  reserved  under  the  copyright  laws  of the 
 * United States.  Use  of a copyright notice is precautionary only and 
 * does not imply publication or disclosure.                            
 *                                                                      
 * THIS DOCUMENTATION CONTAINS CONFIDENTIAL AND PROPRIETARY INFORMATION 
 * OF    VICARIOUS   VISIONS,  INC.    ANY  DUPLICATION,  MODIFICATION, 
 * DISTRIBUTION, OR DISCLOSURE IS STRICTLY PROHIBITED WITHOUT THE PRIOR 
 * EXPRESS WRITTEN PERMISSION OF VICARIOUS VISIONS, INC.
 */

#include "../server/exe_headers.h"
#include "../client/client.h"
#include "../win32/win_local.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/fixedmap.h"
#include "../zlib/zlib.h"
#include "../qcommon/files.h"
#include "xb_log.h"

/***********************************************
*
* WINDOWS/XBOX VERSION
*
* Build a translation table, CRC -> file name.  We have the memory.
*
************************************************/

#if defined(_WINDOWS)
#include <windows.h>
#elif defined(_XBOX)
#include <xtl.h>
#endif

struct FileInfo
{
	char* name;
	int size;
};
static VVFixedMap< FileInfo, unsigned int >* s_Files = NULL;
static byte* buffer;

HANDLE s_Mutex = INVALID_HANDLE_VALUE;

int _buildFileList(const char* path, bool insert, bool buildList)
{
	WIN32_FIND_DATA data;
	char spec[MAX_OSPATH];
	int count = 0;

	// Look for all files
	Com_sprintf(spec, sizeof(spec), "%s\\*.*", path);

	HANDLE h = FindFirstFile(spec, &data);
	while (h != INVALID_HANDLE_VALUE)
	{
		char full[MAX_OSPATH];
		Com_sprintf(full, sizeof(full), "%s\\%s", path, data.cFileName);

		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// Directory -- lets go recursive
			if (data.cFileName[0] != '.') {
				count += _buildFileList(full, insert, buildList);
			}
		}
		else
		{

			if(insert || buildList)
			{
				// Regular file -- add it to the table
				strlwr(full);
				unsigned int code = crc32(0, (const byte *)full, strlen(full));

				FileInfo info;
				info.name = CopyString(full);
				info.size = data.nFileSizeLow;

				if(insert)
				{
					s_Files->Insert(info, code);
				}

				if(buildList)
				{
					// get the length of the filename
					int len;
					len = strlen(info.name) + 1;

					// save the file code
					*(int*)buffer		=  code;
					buffer				+= sizeof(code);

					// save the name of the file
					strcpy((char*)buffer,info.name);
					buffer				+= len;

					// save the size of the file
					*(int*)buffer		= info.size;
					buffer				+= sizeof(info.size);
				}
			}

			count++;
		}

		// Continue the loop
		if (!FindNextFile(h, &data))
		{
			FindClose(h);
			return count;
		}
	}
	return count;
}

bool _buildFileListFromSavedList(void)
{
	XBLog_Write("JAMP: _buildFileListFromSavedList enter");
	// open the file up for reading
	FILE*	in;
	XBLog_Write("JAMP: _buildFileListFromSavedList before fopen");
	in = fopen("d:\\xbx_filelist","rb");
	if(!in)
	{
		XBLog_Write("JAMP: _buildFileListFromSavedList fopen failed");
		return false;
	}

	// read in the number of files
	int count;
	XBLog_Write("JAMP: _buildFileListFromSavedList before count read");
	if(!(fread(&count,sizeof(count),1,in)))
	{
		XBLog_Write("JAMP: _buildFileListFromSavedList count read failed");
		fclose(in);
		return false;
	}

	// allocate memory for a temp buffer
	byte*	baseAddr;
	int bufferSize;
	bufferSize	= count * ( 2 * sizeof(int) + MAX_OSPATH );
	XBLog_Write("JAMP: _buildFileListFromSavedList before Z_Malloc");
	buffer		= (byte*)Z_Malloc(bufferSize,TAG_TEMP_WORKSPACE,qtrue,32);
	XBLog_Write("JAMP: _buildFileListFromSavedList after Z_Malloc");
	baseAddr	= buffer;

	// read the rest of the file into a big buffer
	XBLog_Write("JAMP: _buildFileListFromSavedList before data read");
	if(!(fread(buffer,bufferSize,1,in)))
	{
		XBLog_Write("JAMP: _buildFileListFromSavedList data read failed");
		fclose(in);
		Z_Free(baseAddr);
		return false;
	}

	// allocate some memory for s_Files
	XBLog_Write("JAMP: _buildFileListFromSavedList before map new");
	s_Files = new VVFixedMap<FileInfo, unsigned int>(count);
	XBLog_Write("JAMP: _buildFileListFromSavedList after map new");

	// loop through all the files write out the codes
	int i;
	XBLog_Write("JAMP: _buildFileListFromSavedList before insert loop");
	for(i = 0; i < count; i++)
	{
		FileInfo info;
		unsigned int code;

		// read the code for the file
		code	=  *(int*)buffer;
		buffer	+= sizeof(code);

		// read the filename
		info.name = CopyString((char*)buffer);
		buffer	+= (strlen(info.name) + 1);

		// read the size of the file
		info.size	=  *(int*)buffer;
		buffer		+= sizeof(info.size);

		// save the data - optimization: don't check for dupes!
		s_Files->InsertUnsafe(info, code);
	}
	XBLog_Write("JAMP: _buildFileListFromSavedList after insert loop");

	fclose(in);
	Z_Free(baseAddr);
	XBLog_Write("JAMP: _buildFileListFromSavedList exit true");
	return true;
}

bool Sys_SaveFileCodes(void)
{
	XBLog_Write("JAMP: Sys_SaveFileCodes enter");
	bool ret;
	int res;

	// get the number of files
	int count;
	XBLog_Write("JAMP: Sys_SaveFileCodes before count build");
	count = _buildFileList(Sys_Cwd(), false, false);
	XBLog_Write("JAMP: Sys_SaveFileCodes after count build");

	// open a file for writing
	FILE* out;
	XBLog_Write("JAMP: Sys_SaveFileCodes before fopen");
	out = fopen("d:\\xbx_filelist","wb");
	if(!out)
	{
		XBLog_Write("JAMP: Sys_SaveFileCodes fopen failed");
		return false;
	}

	// allocate a buffer for writing
	byte*	baseAddr;
	int		bufferSize;
	
	bufferSize	= sizeof(int) + ( count * ( 2 * sizeof(int) + MAX_OSPATH ) );
	XBLog_Write("JAMP: Sys_SaveFileCodes before Z_Malloc");
	baseAddr	= (byte*)Z_Malloc(bufferSize,TAG_TEMP_WORKSPACE,qtrue,32);
	XBLog_Write("JAMP: Sys_SaveFileCodes after Z_Malloc");
	buffer		= baseAddr;

	// write the number of files to the buffer
	*(int*)buffer	=  count;
	buffer			+= sizeof(count);

	// fill up the rest of the buffer
	XBLog_Write("JAMP: Sys_SaveFileCodes before list build");
	ret = _buildFileList(Sys_Cwd(), false, true);
	XBLog_Write("JAMP: Sys_SaveFileCodes after list build");

	if(!ret)
	{
		// there was a problem
		XBLog_Write("JAMP: Sys_SaveFileCodes list build failed");
		fclose(out);
		Z_Free(baseAddr);
		return false;
	}

	// attempt to write out the data
	XBLog_Write("JAMP: Sys_SaveFileCodes before fwrite");
	if(!(fwrite(baseAddr,bufferSize,1,out)))
	{
		// there was a problem
		XBLog_Write("JAMP: Sys_SaveFileCodes fwrite failed");
		fclose(out);
		Z_Free(baseAddr);
		return false;
	}

	// everything went ok
	fclose(out);
	Z_Free(baseAddr);
	XBLog_Write("JAMP: Sys_SaveFileCodes exit true");
	return true;
}

void Sys_InitFileCodes(void)
{
	XBLog_Write("JAMP: Sys_InitFileCodes enter");
	bool ret;
	int count = 0;

	// First: try to load an existing filecode cache
	XBLog_Write("JAMP: Sys_InitFileCodes before saved list");
	ret = _buildFileListFromSavedList();
	XBLog_Write("JAMP: Sys_InitFileCodes after saved list");

	// if we had trouble building the list that way
	// we need to do it by searching the files
	if( !ret )
	{
		// There was no filelist cache, make one
		XBLog_Write("JAMP: Sys_InitFileCodes before save filecodes");
		if( !Sys_SaveFileCodes() )
		{
			Com_Printf("WARNING: Couldn't create filecode cache - continuing without it\n");
			XBLog_Write("JAMP: Sys_InitFileCodes save failed nonfatal");
			s_Mutex = CreateMutex(NULL, FALSE, NULL);
			return;
		}
		XBLog_Write("JAMP: Sys_InitFileCodes after save filecodes");

		// Now re-read it
		XBLog_Write("JAMP: Sys_InitFileCodes before reread saved list");
		if( !_buildFileListFromSavedList() )
		{
			Com_Printf("WARNING: Couldn't re-read filecode cache - continuing without it\n");
			XBLog_Write("JAMP: Sys_InitFileCodes reread failed nonfatal");
			s_Mutex = CreateMutex(NULL, FALSE, NULL);
			return;
		}
		XBLog_Write("JAMP: Sys_InitFileCodes after reread saved list");
	}
	XBLog_Write("JAMP: Sys_InitFileCodes before sort");
	s_Files->Sort();
	XBLog_Write("JAMP: Sys_InitFileCodes after sort");

	// make it thread safe
	s_Mutex = CreateMutex(NULL, FALSE, NULL);
	XBLog_Write("JAMP: Sys_InitFileCodes exit");
}

void Sys_ShutdownFileCodes(void)
{
	FileInfo*	info = NULL;

	info = s_Files->Pop();
	while(info)
	{
		Z_Free(info->name);
		info->name = NULL;
		info = s_Files->Pop();
	}

	delete s_Files;
	s_Files = NULL;

	CloseHandle(s_Mutex);
}

int Sys_GetFileCode(const char* name)
{
	WaitForSingleObject(s_Mutex, INFINITE);

	// Get system level path
	char* osname = FS_BuildOSPath(name);
	
	// Generate hash for file name
	strlwr(osname);
	unsigned int code = crc32(0, (const byte *)osname, strlen(osname));
	
	// Check if the file exists
	if (!s_Files->Find(code))
	{
		ReleaseMutex(s_Mutex);
		return -1;
	}

	ReleaseMutex(s_Mutex);
	return code;
}

const char* Sys_GetFileCodeName(int code)
{
	WaitForSingleObject(s_Mutex, INFINITE);

	FileInfo *entry = s_Files->Find(code);
	if (entry)
	{
		ReleaseMutex(s_Mutex);
		return entry->name;
	}
	
	ReleaseMutex(s_Mutex);
	return NULL;
}

int Sys_GetFileCodeSize(int code)
{
	WaitForSingleObject(s_Mutex, INFINITE);

	FileInfo *entry = s_Files->Find(code);
	if (entry)
	{
		ReleaseMutex(s_Mutex);
		return entry->size;
	}
	
	ReleaseMutex(s_Mutex);
	return -1;
}
// Quick function to re-scan for new files, update the filecode
// table, and dump the new one to disk
void Sys_FilecodeScan_f( void )
{
	// Make an updated filecode cache
	if( !Sys_SaveFileCodes() )
		Com_Error( ERR_DROP, "ERROR: Couldn't create filecode cache\n" );

	// Throw out our current list
	Sys_ShutdownFileCodes();

	// Re-init, which should use the new list we just made
	Sys_InitFileCodes();
}
