/*****************************************************************************\
*                                                                             *
*   Filename:       backnum.c                                                 *
*                                                                             *
*   Description:    Backup a file, using chronological numbers extensions.    *
*                                                                             *
*   Notes:          To build in Unix/Linux, copy debugm.h into your ~/include *
*		    directory or equivalent, then run commands like:	      *
*		    export C_INCLUDE_PATH=~/include			      *
*		    gcc dirsize.c -o dirsize	# Release mode version	      *
*		    gcc -D_DEBUG dirsize.c -o dirsize.debug	# Debug ver.  *
*		    							      *
*		    To build in DOS/Windows with MSVC tools, copy the	      *
*		    necessary C library extensions:			      *
*		    stdint.h		Fixed-width integer types	      *
*		    inttypes.h		Fixed-width integers management	      *
*		    dirent.h		Directory entry definitions	      *
*		    dirent.c		Directory entry management	      *
*		    fnmatch.h		Function name matching definitions    *
*		    fnmatch.c		Implement fnmatch().                  *
*		    Then compile both .c files and include them in a library. *
*		    Quick and dirty alternative: Change in include directives *
*		    below dirent.h -> dirent.c, and fnmatch.h -> fnmatch.c.   *
*                                                                             *
*   History:                                                                  *
*    1987-05-07 JFL Initial implementation in Lattice C                       *
*    1992-05-20 JFL Adapted to Microsoft C.                                   *
*    1993-10-19 JFL Cleanup for reuse in other programs                       *
*    1994-04-20 JFL Ported to OS/2.                                           *
*    2005-08-03 JFL Ported to Win32. Added usual switches. Version 1.1.       *
*    2011-05-12 JFL Display the name of the backup file by default.           *
*                   Added the -q option to prevent that.                      *
*                   Use an OS-independant method to copy the file time.       *
*                   Version 1.2.                                              *
*    2011-05-23 JFL Merged in changes from dirc, using standard C99 int types.*
*    2011-06-05 JFL Added the -X option.                                      *
*    2012-05-22 JFL Rewrote using the standard directory access functions.    *
*    2012-05-23 JFL Finalized support for Unix.                               *
*                   Fixed a minor case preservation bug: If the case of the   *
*                   argument did not match the case of the file name, then    *
*                   the generated backup file name used the argument name,    *
*                   not the actual file base name.                            *
*                   Version 2.0.                                              *
*    2012-10-18 JFL Added my name in the help. Version 2.0.1.                 *
*    2013-03-24 JFL Rebuilt with MsvcLibX.lib with support for UTF-8 names.   *
*		    Version 2.1.					      *
*    2014-12-04 JFL Rebuilt with MsvcLibX support for WIN32 paths > 260 chars.*
*                   Updated help. Version 2.1.1.                              *
*    2015-01-08 JFL Work around issue with old versions of Linux that define  *
*                   lchmod and lutimes, but implement only stubs that always  *
*                   fail. Version 2.1.2.                                      *
*    2016-01-08 JFL Fixed all warnings in Linux, and a few real bugs.         *
*		    Version 2.1.3.  					      *
*                                                                             *
\*****************************************************************************/

#define PROGRAM_VERSION "2.1.3"
#define PROGRAM_DATE    "2016-01-08"

#define _CRT_SECURE_NO_WARNINGS 1 /* Avoid Visual C++ 2005 security warnings */

#define __STDC_LIMIT_MACROS /* Make sure C99 macros are defined in C++ */
#define __STDC_CONSTANT_MACROS

#define _GNU_SOURCE
#define _LARGEFILE_SOURCE 1	/* Force using 64-bits file sizes if possible */
#define _FILE_OFFSET_BITS 64	/* Force using 64-bits file sizes if possible */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <limits.h>
#include <stdint.h> /* Actually we just need stdint.h, but Tru64 doesn't have it */
#ifndef UINTMAX_MAX /* For example Tru64 doesn't define it */
typedef unsigned long uintmax_t;
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <time.h>
#include <sys/time.h>		/* For lutimes() */
#include <dirent.h>		/* We use the DIR type and the dirent structure */
#include <fnmatch.h>		/* We use wild card file name matching */
#ifndef FNM_MATCH /* fnmatch.h always defines FNM_NOMATCH, but not always FNM_MATCH */
#define FNM_MATCH 0
#endif
#include <unistd.h>		/* For chdir() */
#include <errno.h>

/* MsvcLibX debugging macros */
#include "debugm.h"

DEBUG_GLOBALS	/* Define global variables used by debugging macros. (Necessary for Unix builds) */

#define VERSION PROGRAM_VERSION " " PROGRAM_DATE " " OS_NAME DEBUG_VERSION

/* A convenient macro */
#define streq(s1, s2) (!strcmp(s1, s2)) /* For the main test routine only */

/************************ Win32-specific definitions *************************/

#ifdef _WIN32		/* Automatically defined when targeting a Win32 app. */

#if defined(__MINGW64__)
#define OS_NAME "MinGW64"
#elif defined(__MINGW32__)
#define OS_NAME "MinGW32"
#elif defined(_WIN64)
#define OS_NAME "Win64"
#else
#define OS_NAME "Win32"
#endif

#define DIRSEPARATOR '\\'
#define PATHNAME_SIZE FILENAME_MAX
#define NODENAME_SIZE FILENAME_MAX
#define PATTERN_ALL "*.*"     		/* Pattern matching all files */

#pragma warning(disable:4996)	/* Ignore the deprecated name warning */

#endif /* _WIN32 */


/************************ MS-DOS-specific definitions ************************/

#ifdef _MSDOS		/* Automatically defined when targeting an MS-DOS app. */

#define OS_NAME "DOS"

#define DIRSEPARATOR '\\'
/* #define PATHNAME_SIZE FILENAME_MAX */
#define PATHNAME_SIZE 255		/* ~~jfl 2000/12/04 Thanks to chdirx(). */
#define NODENAME_SIZE 13                /* 8.3 name length = 8+1+3+1 = 13 */
#define PATTERN_ALL "*.*"     		/* Pattern matching all files */

#endif /* _MSDOS */

/************************* OS/2-specific definitions *************************/

#ifdef _OS2       /* To be defined on the command line for the OS/2 version */

#define OS_NAME "OS/2"

#define DIRSEPARATOR '\\'
#define PATHNAME_SIZE CCHMAXPATH        /* FILENAME_MAX incorrect in stdio.h */
#define NODENAME_SIZE CCHMAXPATHCOMP
#define PATTERN_ALL "*.*"     		/* Pattern matching all files */

#endif /* _OS2 */

/************************* Unix-specific definitions *************************/

#ifdef __unix__		/* Automatically defined when targeting a Unix app. */

#if defined(__CYGWIN64__)
#define OS_NAME "Cygwin64"
#elif defined(__CYGWIN32__)
#define OS_NAME "Cygwin"
#elif defined(__linux__)
#define OS_NAME "Linux"
#else
#define OS_NAME "Unix"
#endif

#define DIRSEPARATOR '/'
#define PATHNAME_SIZE FILENAME_MAX
#define NODENAME_SIZE FILENAME_MAX
#define PATTERN_ALL "*"     		/* Pattern matching all files */

#define _MAX_PATH  FILENAME_MAX
#define _MAX_DRIVE 3
#define _MAX_DIR   FILENAME_MAX
#define _MAX_FNAME FILENAME_MAX
#define _MAX_EXT   FILENAME_MAX

#define _stricmp strcasecmp

#define LocalFileTime localtime

/* Redefine Microsoft-specific _splitpath() and _makepath() */
void _splitpath(const char *path, char *d, char *p, char *n, char *x);
void _makepath(char *buf, const char *d, const char *p, const char *n, const char *x);

#endif /* __unix__ */

/*********************** End of OS-specific definitions **********************/

#define FALSE 0
#define TRUE 1

/* Global variables */

int iVerbose = FALSE;
int iQuiet = FALSE;
int iExec = TRUE;
#ifdef _MSDOS
int iAppend = 0;	/* Replace the extension */
#else
int iAppend = 1;	/* Append the extension */
#endif

/* Prototypes */

void usage(void);
int IsSwitch(char *pszArg);
int fcopy(char *name2, char *name1);
int copydate(char *name2, char *name1);

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:	    main						      |
|									      |
|   Description:    Main program routine				      |
|									      |
|   Parameters:     int argc		    Number of arguments 	      |
|		    char *argv[]	    List of arguments		      |
|									      |
|   Returns:	    The return code to pass to the OS.			      |
|									      |
|   Notes:								      |
|									      |
|   History:								      |
*									      *
\*---------------------------------------------------------------------------*/

#ifdef _MSC_VER
#pragma warning(disable:4706) /* Ignore the "assignment within conditional expression" warning */
#endif

int main(int argc, char *argv[]) {
  char *pszMyFile = NULL;
  char szPath[_MAX_PATH];
  char szDrive[_MAX_DRIVE];
  char szDir[_MAX_DIR];
  char *pszDir = szDir;
  char szFname[_MAX_FNAME];
  char szExt[_MAX_EXT];
  char szBasename[_MAX_PATH];		/* Initial szFname + szExt */
  int err;
  int iOffset;
  int iMax;
  int i;
  char *pszExactCaseNameFound = NULL;
  char *pszOtherCaseNameFound = NULL;

  for (i=1 ; i<argc ; i++) {
    if (IsSwitch(argv[i])) {   		/* It's a switch */
      if (   streq(argv[i]+1, "?")
	  || streq(argv[i]+1, "h")
	  || streq(argv[i]+1, "-help")
	 ) {				/* -?: Help */
	usage();                        	/* Display help */
	return 0;
      }
#ifndef _MSDOS
      if (streq(argv[i]+1, "a")) {		/* -a: Append the extension */
	iAppend = TRUE;
	continue;
      }
      if (streq(argv[i]+1, "A")) {		/* -A: Replace the extension */
	iAppend = FALSE;
	continue;
      }
#endif
#ifdef _DEBUG
      if (streq(argv[i]+1, "d")) {	/* -d: Debug information */
	DEBUG_ON();
	continue;
      }
#endif
      if (streq(argv[i]+1, "q")) {		/* -q: Be quiet */
	iQuiet = TRUE;
	continue;
      }
#ifdef _DEBUG
      if (streq(argv[i]+1, "t")) {
	closedir(opendir(argv[i+1]));
	exit(0);
      }
#ifdef _MSDOS
      if (streq(argv[i]+1, "T")) {
	opendir("");
	for (i=0; i<_sys_nerr; i++) printf("errno=%d: %s\n", i, strerror(i));
	exit(0);
      }
#endif
#endif
      if (streq(argv[i]+1, "v")) {		/* -v: Verbose information */
	iVerbose = TRUE;
	continue;
      }
      if (streq(argv[i]+1, "V")) {		/* -V: Display the version */
	printf("%s\n", VERSION);
	exit(0);
      }
      if (streq(argv[i]+1, "X")) {		/* -X: Do not execute */
	iExec = FALSE;
	continue;
      }
      /* Unsupported switch! */
      fprintf(stderr, "Unsupported switch, ignored: %s", argv[i]);
      continue;
    }
    if (!pszMyFile) {
      pszMyFile = argv[i];
      continue;
    }
    /* Unsupported argument */
    fprintf(stderr, "Unsupported argument, ignored: %s", argv[i]);
  }

  if (!pszMyFile) {
    usage();
    exit(0);
  }

  /* Check if the specified file exists. */
  /* Note: The file name case matching is file system specific, _not_ OS specific.
	   Use the access() result as a proof of existence and readability,
	   whether the case matches or not, and independantly of the host OS. */
  if (access(pszMyFile, R_OK)) {
    fprintf(stderr, "Error: File %s: %s!\n", pszMyFile, strerror(errno));
    exit(1);
  }

  _splitpath(pszMyFile, szDrive, szDir, szFname, szExt);
  DEBUG_PRINTF(("\"%s\" \"%s\" \"%s\" \"%s\"\n", szDrive, szDir, szFname, szExt));
  _makepath(szBasename, NULL, NULL, szFname, szExt);
  DEBUG_PRINTF(("szBasename = \"%s\";\n", szBasename));
  if (iAppend && szExt[0]) {
   strcat(szFname, szExt); /* The period is already in szExt */
  }
  if (!szDir[0]) pszDir = ".";

  /* Now scan all existing backups, to find the largest backup number used. */
  _makepath(szPath, szDrive, pszDir, NULL, NULL);
  iOffset = (int)strlen(szFname) + 1;
  DEBUG_PRINTF(("iOffset = %d; // Backup suffix offset\n", iOffset));
  iMax = 0;

  {
    DIR *pDir;
    struct dirent *pDE;
    char szPattern[_MAX_PATH];

    pDir = opendir(szPath);
    if (!pDir) {
      fprintf(stderr, "Error: Directory %s: %s\n", szPath, strerror(errno));
      exit(1);
    }

    _makepath(szPattern, NULL, NULL, szFname, "*");

    while ((pDE = readdir(pDir))) {
      if (pDE->d_type != DT_REG) continue;	/* We want only files */

      /* Check the case of the base file */
      if (!_stricmp(szBasename, pDE->d_name)) {
      	if (!strcmp(szBasename, pDE->d_name)) { /* This can happen only once */
      	  pszExactCaseNameFound = strdup(pDE->d_name);
	  DEBUG_PRINTF(("// Found base name with exact case: \"%s\"\n", pszExactCaseNameFound));
      	} else if (!pszOtherCaseNameFound) {	/* Else can happen many times */
      	  pszOtherCaseNameFound = strdup(pDE->d_name);
	  DEBUG_PRINTF(("// Found base name with != case: \"%s\"\n", pszOtherCaseNameFound));
      	}
      }

      /* Check files that match the wildcard pattern */
      if (fnmatch(szPattern, pDE->d_name, FNM_CASEFOLD) == FNM_MATCH) {
	int i = 0;
	DEBUG_PRINTF(("// Found backup name: \"%s\"\n", pDE->d_name));
	sscanf(pDE->d_name + iOffset, "%03d", &i);
	if (i > iMax) iMax = i;
	DEBUG_PRINTF(("iMax = %d;\n", iMax));
      }
    }

    closedir(pDir);
  }

  /* Correct the file name case if needed */
  /* Note: Unix file systems are case dependant, Microsoft file systems are not.
     But Linux accessing a Microsoft file system across the network will _also_
     be case _independant_. "cat HeLlO.tXt" _will_ successfully read file hello.txt.
     So if the file passed the access() test above, then we _must_ correct the
     case now, for all operating systems _even_ for Unix. */
  strcpy(szPath, pszMyFile);
  if (pszOtherCaseNameFound && !pszExactCaseNameFound) {
    DEBUG_PRINTF(("// Correcting case: \"%s\"\n", pszOtherCaseNameFound));
    _splitpath(pszOtherCaseNameFound, NULL, NULL, szFname, szExt);
    _makepath(szPath, szDrive, szDir, szFname, szExt);
    if (iAppend && szExt[0]) {
     strcat(szFname, szExt); /* The period is already in szExt */
    }
  }
  if (iVerbose && !iQuiet) printf("Backing up %s\n", szPath);

  /* Generate the backup file name */
  sprintf(szExt, ".%03d", iMax+1);
  _makepath(szPath, szDrive, szDir, szFname, szExt);
  if (!iQuiet) printf("%s%s\n", iVerbose ? "        as " : "", szPath);

  /* Backup the file */
  err = 0;
  if (iExec) err = fcopy(szPath, pszMyFile);
  switch (err) {
    case 0: break; /* Success */
    case 1: fprintf(stderr, "Not enough memory.\n"); break;
    case 2: fprintf(stderr, "Error reading from %s.\n", argv[2]); break;
    case 3: fprintf(stderr, "Error writing to %s.\n", argv[3]); break;
    default: {
#if defined(_WIN32)
      LPVOID lpMsgBuf;
      if (FormatMessage(
	FORMAT_MESSAGE_ALLOCATE_BUFFER |
	FORMAT_MESSAGE_FROM_SYSTEM |
	FORMAT_MESSAGE_IGNORE_INSERTS,
	NULL,
	err,
	MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* Default language */
	(LPTSTR) &lpMsgBuf,
	0,
	NULL )) {
	fprintf(stderr, "Error. %s\n", lpMsgBuf);
	LocalFree( lpMsgBuf );     /* Free the buffer */
      } else {
	fprintf(stderr, "Unexpected return value: %d\n", err); break;
      }
#else
      fprintf(stderr, "Unexpected return value: %d\n", err); break;
#endif
      break;
    }
  }

  return err;
}

#ifdef _MSC_VER
#pragma warning(default:4706)
#endif

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:	    usage						      |
|									      |
|   Description:    Display a brief help screen 			      |
|									      |
|   Parameters:     None						      |
|									      |
|   Returns:	    Does not return					      |
|									      |
|   History:								      |
*									      *
\*---------------------------------------------------------------------------*/

void usage(void) {
  printf("\n\
BackNum version " VERSION "\n\
\n\
Usage:\n\
\n\
  backnum [switches] {filename}\n\
\n\
Switches:\n\
\n"
#ifndef _MSDOS
"  -a    Append the backup number to the file name. (Default)\n\
  -A    Replace the extension with the backup number\n"
#endif
#ifdef _DEBUG
"\
  -d    Output debug information.\n"
#endif
"  -q    Be quiet\n\
  -v    Display verbose information\n\
  -X    Display the backup file name, but don't create it.\n\
\n"
#ifdef _MSDOS
"Author: Jean-Francois Larvoire"
#else
"Author: Jean-François Larvoire"
#endif
" - jf.larvoire@hpe.com or jf.larvoire@free.fr\n"
#ifdef __unix__
"\n"
#endif
);
}

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:	    IsSwitch						      |
|                                                                             |
|   Description:    Test if an argument is a command-line switch.             |
|                                                                             |
|   Parameters:     char *pszArg	    Would-be argument		      |
|                                                                             |
|   Return value:   TRUE or FALSE					      |
|                                                                             |
|   Notes:								      |
|                                                                             |
|   History:								      |
*                                                                             *
\*---------------------------------------------------------------------------*/

int IsSwitch(char *pszArg) {
  return (   (*pszArg == '-')
#ifndef __unix__
            || (*pszArg == '/')
#endif
  ); /* It's a switch */
}

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:	    fcopy						      |
|									      |
|   Description:    Copy a file. Preserve date & time.			      |
|									      |
|   Parameters:     char *name2		    Target file name	 	      |
|		    char *name1		    Source file name	 	      |
|									      |
|   Returns:		0	Success					      |
|			1	Not enough memory			      |
|			2	Cannot read from file 1			      |
|			3	Cannot write to file 2			      |
|									      |
|   Notes:								      |
|									      |
|   History:								      |
|    1987/05/07 JFL Initial implementation in Lattice C                       |
|    1992/05/20 JFL Adapted to Microsoft C.                                   |
|    1993/10/19 JFL Cleanup for reuse in other programs                       |
|    2011/05/12 JFL Use an OS-independant method to copy the file time.       |
*									      *
\*---------------------------------------------------------------------------*/

#define BUFFERSIZE 4096

#ifdef _MSC_VER
#pragma warning(disable:4706) /* Ignore the "assignment within conditional expression" warning */
#endif

int fcopy(char *name2, char *name1) {
  FILE *pfs, *pfd;        /* Source & destination file pointers */
  int tocopy;             /* Number of bytes to copy in one pass */
  static char *buffer;    /* Pointer on the intermediate copy buffer */
  int err;

  DEBUG_ENTER(("fcopy(\"%s\", \"%s\");\n", name2, name1));

  if (!buffer) {
    buffer = malloc(BUFFERSIZE);    /* Allocate memory for copying */
    if (!buffer) {
      DEBUG_LEAVE(("return 1; // Out of memory for copy buffer\n"));
      return 1;
    }
  }

  pfs = fopen(name1, "rb");
  if (!pfs) {
    DEBUG_LEAVE(("return 2; // Cannot open source file\n"));
    return 2;
  }

  pfd = fopen(name2, "wb");
  if (!pfd) {
    fclose(pfs);
    DEBUG_LEAVE(("return 3; // Cannot open destination file\n"));
    return 3;
  }

  while ((tocopy = (int)fread(buffer, 1, BUFFERSIZE, pfs))) {
    if (!fwrite(buffer, tocopy, 1, pfd)) {
      fclose(pfs);
      fclose(pfd);
      DEBUG_LEAVE(("return 3; // Cannot write to destination file\n"));
      return 3;
    }
  }

  /* Flush buffers into the destination file, */
  fflush(pfd);
  /* and give the same date than the source file */

  fclose(pfs);
  fclose(pfd);

  /* 2011-05-12 Use an OS-independant method, _after_ closing the files */
  err = copydate(name2, name1);

  DEBUG_LEAVE(("return %d; // Copy successful\n", err));
  return err;
}

#ifdef _MSC_VER
#pragma warning(default:4706)
#endif

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:	    copydate						      |
|									      |
|   Description:    Copy the Date/time stamp from one file to another	      |
|									      |
|   Parameters:     char *pszToFile	Destination file		      |
|		    char *pszFromFile	Source file			      |
|									      |
|   Returns:	    None						      |
|									      |
|   Notes:	    This operation is useless if the destination file is      |
|		    written to again. So it's necessary to flush it before    |
|		    calling this function.				      |
|									      |
|   History:								      |
|									      |
|    1996-10-14 JFL Made a clean, application-independant, version.	      |
|    2011-05-12 JFL Rewrote in an OS-independant way.			      |
|    2015-01-08 JFL Fallback to using chmod and utimes if lchmod and lutimes  |
|                   are not implemented. This will cause minor problems if the|
|                   target is a link, but will work well in all other cases.  |
*									      *
\*---------------------------------------------------------------------------*/

/* Old Linux versions define lchmod, but only implement a stub that always fails */
#ifdef __stub_lchmod
/* #pragma message "The C Library does not implement lchmod. Using our own replacement." */
#define lchmod lchmod1 /* Then use our own replacement for lchmod */
int lchmod1(const char *path, mode_t mode) {
  struct stat st = {0};
  int err;
  DEBUG_PRINTF(("lchmod1(\"%s\", %X);\n", path, mode));
  err = lstat(path, &st);
  if (err) return err;
  /* Assume that libs that bother defining __stub_lchmod all define S_ISLNK */
  if (!S_ISLNK(st.st_mode)) { /* If it's anything but a link */
    err = chmod(path, mode);	/* Then use the plain function supported by all OSs */
  } else { /* Else don't do it for a link, as it's the target that would be modified */
    err = -1;
    errno = ENOSYS;
  }
  return err;
}
#endif

#ifndef _STRUCT_TIMEVAL
/* No support for micro-second file time resolution. Use utime(). */
int copydate(char *pszToFile, char *pszFromFile) { /* Copy the file dates */
  /* Note: "struct _stat" and "struct _utimbuf" don't compile under Linux */
  struct stat stFrom = {0};
  struct utimbuf utbTo = {0};
  int err;
  err = lstat(pszFromFile, &stFrom);
  /* Copy file permissions too */
  err = lchmod(pszToFile, stFrom.st_mode);
  /* And copy file times */
  utbTo.actime = stFrom.st_atime;
  utbTo.modtime = stFrom.st_mtime;
  err = lutime(pszToFile, &utbTo);
  DEBUG_CODE({
    struct tm *pTime;
    char buf[40];
    pTime = LocalFileTime(&(utbTo.modtime)); /* Time of last data modification */
    sprintf(buf, "%4d-%02d-%02d %02d:%02d:%02d",
	    pTime->tm_year + 1900, pTime->tm_mon + 1, pTime->tm_mday,
	    pTime->tm_hour, pTime->tm_min, pTime->tm_sec);
    DEBUG_PRINTF(("utime(\"%s\", %s) = %d %s\n", pszToFile, buf, err,
      		  err?strerror(errno):""));
  });
  return err;                       /* Success */
}
#else /* defined(_STRUCT_TIMEVAL) */

#ifdef __stub_lutimes
/* #pragma message "The C Library does not implement lutimes. Using our own replacement." */
#define lutimes lutimes1 /* Then use our own replacement for lutimes */
int lutimes1(const char *path, const struct timeval times[2]) {
  struct stat st = {0};
  int err;
  /* DEBUG_PRINTF(("lutimes1(\"%s\", %p);\n", path, &times)); // No need for this as VALUEIZE(lutimes) duplicates this below. */
  err = lstat(path, &st);
  if (err) return err;
  /* Assume that libs that bother defining __stub_lutimes all define S_ISLNK */
  if (!S_ISLNK(st.st_mode)) { /* If it's anything but a link */
    err = utimes(path, times);	/* Then use the plain function supported by all OSs */
  } else { /* Else don't do it for a link, as it's the target that would be modified */
    err = -1;
    errno = ENOSYS;
  }
  return err;
}
#endif

/* Micro-second file time resolution supported. Use utimes(). */
int copydate(char *pszToFile, char *pszFromFile) { /* Copy the file dates */
  /* Note: "struct _stat" and "struct _utimbuf" don't compile under Linux */
  struct stat stFrom = {0};
  struct timeval tvTo[2] = {{0}, {0}};
  int err;
  lstat(pszFromFile, &stFrom);
  /* Copy file permissions too */
  err = lchmod(pszToFile, stFrom.st_mode);
  /* And copy file times */
  TIMESPEC_TO_TIMEVAL(&tvTo[0], &stFrom.st_atim);
  TIMESPEC_TO_TIMEVAL(&tvTo[1], &stFrom.st_mtim);
  err = lutimes(pszToFile, tvTo);
#ifndef _MSVCLIBX_H_ /* Trace lutimes() call and return in Linux too */
  DEBUG_CODE({
    struct tm *pTime;
    char buf[40];
    pTime = LocalFileTime(&(stFrom.st_mtime)); /* Time of last data modification */
    sprintf(buf, "%4d-%02d-%02d %02d:%02d:%02d.%06ld",
	    pTime->tm_year + 1900, pTime->tm_mon + 1, pTime->tm_mday,
	    pTime->tm_hour, pTime->tm_min, pTime->tm_sec, (long)stFrom.st_mtim.tv_nsec / 1000);
    DEBUG_PRINTF((VALUEIZE(lutimes) "(\"%s\", %s) = %d\n", pszToFile, buf, err));
  });
#endif
  return err;                       /* Success */
}
#endif /* !defined(_STRUCT_TIMEVAL) */

#ifdef __unix__		/* Automatically defined when targeting a Unix app. */

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Functions:	    _splitpath() and _makepath()			      |
|									      |
|   Description:    Implement Microsoft-specific functions for Unix	      |
|									      |
|   Notes:								      |
|									      |
|   History:								      |
|    2012-05-23 JFL Initial implementation                                    |
*									      *
\*---------------------------------------------------------------------------*/

void _splitpath(const char *path, char *d, char *p, char *n, char *x) {
  size_t len;
  const char *pszName;
  const char *pszExt;
  /* There's no drive under Unix */
  if (d) *d = '\0';
  /* Extract the basename */
  pszName = strrchr(path, '/');	/* Find the directory/name separator */
  if (pszName) {		/* If found */
    pszName += 1;		   /* The name starts immediately afterwards */
  } else {			/* else */
    pszName = path;		   /* The name is all what we have */
  }
  if (n) strcpy(n, pszName);
  /* Extract the dirname */
  len = pszName - path;
  if (p) {
    strncpy(p, path, len);
    p[len] = '\0';
    if (len && (p[len-1] != '/')) strcpy(p+len, "/");
  }
  /* Split the name and extension */
  if (x) *x = '\0';
  pszExt = strrchr(pszName, '.');	/* Find the basename.ext separator */
  if (pszExt) {		/* If found */
    if (x) strcpy(x, pszExt);
    if (n) n[pszExt - pszName] = '\0';
  }
}

void _makepath(char *buf, const char *d, const char *p, const char *n, const char *x) {
  size_t len;
  buf[0] = '\0';
  if (d) strcpy(buf, d);
  if (p) strcat(buf, p);
  len = strlen(buf);
  if (len && (buf[len-1] != '/') && ((n && n[0]) || (x && x[0]))) strcat(buf, "/");
  if (n) strcat(buf, n);
  if (x) {
    if (x[0] && (x[0] != '.')) strcat(buf, ".");
    strcat(buf, x);
  }
}

#endif


