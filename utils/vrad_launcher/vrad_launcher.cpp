// vrad_launcher.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <direct.h>
#include "vstdlib/strtools.h"
#include "vstdlib/icommandline.h"


char* GetLastErrorString()
{
	static char err[2048];
	
	LPVOID lpMsgBuf;
	FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL 
	);

	strncpy( err, (char*)lpMsgBuf, sizeof( err ) );
	LocalFree( lpMsgBuf );

	err[ sizeof( err ) - 1 ] = 0;

	return err;
}


void MakeFullPath( const char *pIn, char *pOut, int outLen )
{
	if ( pIn[0] == '/' || pIn[0] == '\\' || pIn[1] == ':' )
	{
		// It's already a full path.
		Q_strncpy( pOut, pIn, outLen );
	}
	else
	{
		_getcwd( pOut, outLen );
		Q_strncat( pOut, "\\", outLen );
		Q_strncat( pOut, pIn, outLen );
	}
}


void StripFilename( char *pFilename )
{
	char *pLastSlash = 0;
	while ( *pFilename )
	{
		if ( *pFilename == '/' || *pFilename == '\\' )
			pLastSlash = pFilename;

		++pFilename;
	}
	if ( pLastSlash )
		*pLastSlash = 0;
}


int main(int argc, char* argv[])
{
	char dllName[512];
	bool bUseDefault = true;

	CommandLine()->CreateCmdLine( argc, argv );

	char fullPath[512], redirectFilename[512];
	MakeFullPath( argv[0], fullPath, sizeof( fullPath ) );
	StripFilename( fullPath );
	Q_snprintf( redirectFilename, sizeof( redirectFilename ), "%s\\%s", fullPath, "vrad.redirect" );

	// First, look for vrad.redirect and load the dll specified in there if possible.
	CSysModule *pModule = NULL;
	FILE *fp = fopen( redirectFilename, "rt" );
	if ( fp )
	{
		if ( fgets( dllName, sizeof( dllName ), fp ) )
		{
			char *pEnd = strstr( dllName, "\n" );
			if ( pEnd )
				*pEnd = 0;

			pModule = Sys_LoadModule( dllName );
			if ( pModule )
				printf( "Loaded alternate VRAD DLL (%s) specified in vrad.redirect.\n", dllName );
			else
				printf( "Can't find '%s' specified in vrad.redirect.\n", dllName );
		}
		
		fclose( fp );
	}

	// If it didn't load the module above, then use the 
	if ( !pModule )
	{
		strcpy( dllName, "vrad.dll" );
		pModule = Sys_LoadModule( dllName );
	}

	if( !pModule )
	{
		printf( "vrad_launcher error: can't load %s\n%s", dllName, GetLastErrorString() );
		return 1;
	}

	CreateInterfaceFn fn = Sys_GetFactory( pModule );
	if( !fn )
	{
		printf( "vrad_launcher error: can't get factory from vrad.dll\n" );
		Sys_UnloadModule( pModule );
		return 2;
	}

	int retCode = 0;
	IVRadDLL *pDLL = (IVRadDLL*)fn( VRAD_INTERFACE_VERSION, &retCode );
	if( !pDLL )
	{
		printf( "vrad_launcher error: can't get IVRadDLL interface from vrad.dll\n" );
		Sys_UnloadModule( pModule );
		return 3;
	}

	int returnValue = pDLL->main( argc, argv );
	Sys_UnloadModule( pModule );
	
	return returnValue;
}

