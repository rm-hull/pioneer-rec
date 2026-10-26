/*****************************************************************************
 * Pioneer DVR-633H movie file recovery                                      *
 *                                                                           *
 * (C) by leecher@dose.0wnz.at 2014                                          *
 *****************************************************************************
 * Module:      Main application                                             *
 * File:        pioneer_rec.c                                                *
 * Description: Reads the supplied disk image file and tries to reconstruct  *
 *              the movie files from it parsing the homemade (?) Pioneer DVR *
 *              filesystem.                                                  *
 * Version:     1.0                                                          *
 * Info:        http://code.google.com/p/pioneer-rec                         *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>

/* Fixed-width type aliases (must come before struct definitions) */
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;

/* Byte-swap macros (gcc/clang builtins) */
#if defined(__GNUC__) || defined(__clang__)
	#define B2N_32(x) __builtin_bswap32((uint32_t)(x))
	#define B2N_16(x) __builtin_bswap16((uint16_t)(x))
#else
	#define B2N_32(x) (((uint32_t)(x) << 24) | \
	                   (((uint32_t)(x) << 8) & 0x00FF0000U) | \
	                   (((uint32_t)(x) >> 8) & 0x0000FF00U) | \
	                   ((uint32_t)(x) >> 24))
	#define B2N_16(x) (((uint16_t)(x) << 8) | ((uint16_t)(x) >> 8))
#endif


#define BLOCK_SIZE		2048		// Data is allocated in 2K blocks
#define SECT_SIZE		512			// A sector always has 512 bytes

#define VOB_MAGIC		0xBA010000U	// Start of a MPEG Pack header
#define SCR_THRESH_X	0x600U		// Default threshold SCR stamps on extract
#define SCR_THRESH_M	5U			// Default threshold SCR stamps on merge
#define MAX_DIRSRCH		0x5000000U	// Heuristic: Search up to this offset for diretory

#pragma pack(1)

// This seems to be an address on disk consising of a block number and unknown additional bytes
typedef struct {
	DWORD block;	// -> Points to the Block on disk
	DWORD unk1;
} ADDRESS;

/* A list ob blocks the file consists of */
typedef struct {
	DWORD block;	// Block that contains the data
	WORD blocks;	// Number of blocks the frame consists of, remove highest bit (keyframe?)
	WORD unk;
} BLISTENTRY;

typedef struct {
	WORD yy;	// Year
	BYTE mm;	// Month
	BYTE dd;	// Day
	BYTE hh;	// Hour
	BYTE mi;	// Minute
	BYTE ss;	// Second
	BYTE pad;
} TIMESTAMP;

typedef WORD INODEINDEX;

#pragma pack()

#ifndef DVR
#define DVR 633
#endif

#if DVR == 520
#include "dvr520h.h"
#elif DVR == 545
#include "dvr545h.h"
#elif DVR == 550
#include "dvr550h.h"
#else
#include "dvr633h.h"
#endif

/* Constants */
#define FNTDIR_ENTRIES   ((SECT_SIZE/sizeof(ADDRESS))*2)
#define BLOCKDIR_ENTRIES (BLOCK_SIZE/sizeof(ADDRESS))
#define FATDIR_ENTRIES   (BLOCK_SIZE/sizeof(ADDRESS))
#define FOTDIR_ENTRIES   ((1<<(sizeof(WORD)*8))/FOT_ENTRIES)
#define FOT_ENTRIES		 (BLOCK_SIZE/sizeof(FOTENTRY))
#define FNT_ENTRIES      (BLOCK_SIZE/sizeof(FNTENTRY))
#define FAT_ENTRIES		 (BLOCK_SIZE/sizeof(FATENTRY))
#define BLIST_ENTRIES    (BLOCK_SIZE/sizeof(BLISTENTRY))

/* Helper: read exactly 'len' bytes at 'offset' from file 'fp' into 'buf' using pread */
static int rd_at(FILE *fp, off_t offset, void *buf, size_t len)
{
	if (fseeko(fp, offset, SEEK_SET) != 0)
		return 0;
	size_t n = fread(buf, 1, len, fp);
	return (n == len) ? 1 : (n > 0 ? 1 : 0);
}

/* Prototypes */
static int myPathCleanupSpec(char *pszSpec);
static void dumpFNTEntry(char *pszDstDir, int idx, FNTENTRY *fnt, TIMESTAMP *ts);
static void printFNTEntry(char *pszDstDir, int idx, FNTENTRY *fnt, TIMESTAMP *ts);
static bool dumpFile(FILE *fp, FILE *fpOut, FATENTRY *fat);

/****************************************************************************
 *                                P U B L I C                               *
 ****************************************************************************/

/* Show usage information for commandline program
 *
 * Params:
 *	pszCmd		- Filename to show, usually argv[0]
 * Returns:
 *	EXIT_FAILURE
 */
int usage(char *pszCmd)
{
	printf ("Usage: %s [-s<x|m> <SCR threshold>] [-o <Offset>] [-c <Start-Cnt>]\n"
		"\t<-l> <src file> <-D>\n"
		"\t<-d|-x|-p|-L> <src file> <dest dir>\n"
		"\t<-m> <dest dir>\n\n"
		"Options:\n"
		"\t-s\tSpecifies the System clock Reference (SCR) threshold that\n"
		"\t\tconsiders consecutive MPEG1-chunks to belong together. Normally\n"
		"\t\tyou don't need to change these values. x and m specifies the\n"
		"\t\tthreshold for the given operation:\n"
		"\t-sx\tSpecifies threshold for -x (extract), Default: %d\n"
		"\t-sm\tSpecifies threshold for -m (merge), Default: %d\n"
		"\t-o\tSpecifies offset in <src file> where to start search, Def.: 0\n"
		"\t-c\t-x,-p: Specifies start counter of MPEG-File naming, Def.:0\n"
		"\t-D\tOn extraction, use dub mode (keeps dev. editings)\n"
		"\t\t-d,-l,-L: Specifies file name table entry to start with, Def.:0\n\n"
		"Operations (mandatory):\n"
		"\t-l\tList names of recordings found in <src file>\n"
		"\t-L\tCreate directories for found recordings in <dest dir>\n"
		"\t-d\tProcess directory list and dump all movies from <src file>\n"
		"\t\tto <dest dir>\n"
		"\t-x\tExtract MPEG-Chunks from <src file> to <dest dir>\n"
		"\t-m\tMerge MPEG-Chunks by SCR in <dest dir>\n"
		"\t-p\tProcess (Extract & Merge)\n\n"
		"If your directory structure and image isn't garbled, you should use -d\n"
		"In case your image is severely damaged, you can try -p instead.\n\n"
		"Example: %s -d pioneer.img /path/to/dump\n", pszCmd, SCR_THRESH_X, SCR_THRESH_M,
		pszCmd);
	return EXIT_FAILURE;
}

//---------------------------------------------------------------------------

/* Extracts movie chunks aligned at CHUNK_SIZE from image to files
 *
 * Params:
 *	pszSrc		- Image file to parse
 *	pszDstDir	- Destination directory where to put the extracted chunks to
 *	dwOffset	- Offset where to start parsing in file, usually 0
 *	dwThresh	- Maximum threshold to consider 2 consecutive MPEG-segmets as
 *				  joinable.
 * Returns:
 *	TRUE on success, FALSE on failure
 */
bool extract(char *pszSrc, char *pszDstDir, uint64_t dwOffset, DWORD fno, DWORD dwThresh)
{
	FILE *fp = NULL, *fpout = NULL;
	off_t imgsz = 0, offset = 0;
	uint32_t lastvob = 0, vob;
	BYTE buf[BLOCK_SIZE];
	char outfile[4096], *pszAction = "Scanning";
	struct stat st;

	/* Check destination directory is writable */
	{
		struct stat dstst;
		if (stat(pszDstDir, &dstst) != 0 || !S_ISDIR(dstst.st_mode))
		{
			fprintf(stderr, "Cannot write to destination dir %s: %s\n", pszDstDir, strerror(errno));
			return false;
		}
	}

	fp = fopen(pszSrc, "rb");
	if (!fp)
	{
		fprintf(stderr, "Cannot open source file %s: %s\n", pszSrc, strerror(errno));
		return false;
	}

	/* Get file size */
	if (fstat(fileno(fp), &st) == 0)
		imgsz = (off_t)st.st_size;

	/* Seek to offset */
	if (dwOffset && fseeko(fp, (off_t)dwOffset, SEEK_SET) != 0)
	{
		fprintf(stderr, "Cannot seek to offset %llu: %s\n", (unsigned long long)dwOffset, strerror(errno));
		fclose(fp);
		return false;
	}
	offset = (off_t)dwOffset;

	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
	{
		if ((offset % (100 * BLOCK_SIZE)) == 0 && imgsz > 0)
			printf ("\r%s %llu/%llu (%d%%)...", pszAction, (unsigned long long)offset, (unsigned long long)imgsz,
				(int)(((double)offset / (double)imgsz) * 100));

		/* Check for VOB magic using safe unaligned read */
		if (read_u32(buf) == VOB_MAGIC && n >= 8)
		{
			pszAction = "Dumping";
			vob = B2N_32(read_u32(&buf[4]));
			if (vob > lastvob + dwThresh || vob < lastvob || lastvob == 0)
			{
				if (fpout) fclose(fpout);
				snprintf(outfile, sizeof(outfile), "%s/%08d.mpg", pszDstDir, fno);
				printf ("\rNew video stream found at offset %08llx ", (unsigned long long)offset);
				while ((fpout = fopen(outfile, "wb")) != NULL)
				{
					/* File opened successfully (new file) */
					break;
				}
				/* If file already exists, try next number */
				while (fpout == NULL && errno == EEXIST)
				{
					snprintf(outfile, sizeof(outfile), "%s/%08d.mpg", pszDstDir, ++fno);
					fpout = fopen(outfile, "wb");
				}
				if (!fpout)
				{
					fprintf(stderr, "Cannot write to destination file %s: %s\n", outfile, strerror(errno));
					fclose(fp);
					return false;
				}
				printf ("-> %08d.mpg\n", fno);
				fno++;
			}
			if (fwrite(buf, 1, n, fpout) != n)
			{
				fprintf(stderr, "Cannot append to destination file %s: %s\n", outfile, strerror(errno));
				if (fpout) fclose(fpout);
				fclose(fp);
				return false;
			}
			lastvob = vob;
		}
		else
			pszAction = "Scanning";
		offset += (off_t)n;
	}
	printf ("\r%s %llu/%llu (100%%)...", pszAction, (unsigned long long)offset, (unsigned long long)imgsz);
	if (fpout) fclose(fpout);
	fclose(fp);
	printf ("Done.\n");
	return true;
}

//---------------------------------------------------------------------------

/* Merges together the mpeg-chunkfiles found in pszDstDir to create 
 * consecutive movie chunks out of it.
 * Use with care!
 *
 * Params:
 *	pszDstDir	- Directory that contains all the movie chunks that will be
 *				  merged together.
 *	dwStep		- Threshold, chunks with SCR >4 && <dwThreshold are merged 
 *				  together.
 * Returns:
 *	TRUE on success, FALSE on failure
 */
bool merge(char *pszDstDir, DWORD dwStep)
{
	DIR *dir = NULL;
	struct dirent *ent = NULL, *ent2 = NULL;
	char szPath[4096], buf[32768];
	uint32_t vob, vob2;
	bool bRestart;

	dir = opendir(pszDstDir);
	if (!dir)
	{
		fprintf(stderr, "Cannot parse directory %s: %s\n", pszDstDir, strerror(errno));
		return false;
	}

	do
	{
		bRestart = false;
		rewinddir(dir);
		while ((ent = readdir(dir)) != NULL)
		{
			if (ent->d_name[0] == '.')
				continue;
			snprintf(szPath, sizeof(szPath), "%s/%s", pszDstDir, ent->d_name);

			FILE *hFile = fopen(szPath, "r+b");
			if (!hFile)
				continue;

			/* Read SCR from near end of file (-0x800+4 from end) */
			struct stat fst;
			if (fstat(fileno(hFile), &fst) == 0 && fst.st_size > 0x800)
			{
				fseeko(hFile, (off_t)fst.st_size - 0x800 + 4, SEEK_SET);
				vob = 0;
				(void)fread(&vob, sizeof(vob), 1, hFile);
				vob = B2N_32(vob);
			}
			else
			{
				fclose(hFile);
				continue;
			}

			/* Inner loop: find a file to append */
			rewinddir(dir);
			while ((ent2 = readdir(dir)) != NULL)
			{
				if (ent2->d_name[0] == '.' || strcmp(ent2->d_name, ent->d_name) == 0)
					continue;
				snprintf(szPath, sizeof(szPath), "%s/%s", pszDstDir, ent2->d_name);
				printf ("\rMatching %s/%s...", ent->d_name, ent2->d_name);

				FILE *hFile2 = fopen(szPath, "rb");
				if (!hFile2)
					continue;

				fseeko(hFile2, 4, SEEK_SET);
				vob2 = 0;
				(void)fread(&vob2, sizeof(vob2), 1, hFile2);
				vob2 = B2N_32(vob2);

				if (vob2 >= vob + 4 && vob2 <= vob + dwStep)
				{
					bRestart = true;
					fseeko(hFile, 0, SEEK_END);
					fseeko(hFile2, 0, SEEK_SET);

					struct stat fst2;
					off_t imgsz = 0, offset = 0;
					fstat(fileno(hFile2), &fst2);
					imgsz = (off_t)fst2.st_size;

					size_t n2;
					while ((n2 = fread(buf, 1, sizeof(buf), hFile2)) > 0)
					{
						offset += (off_t)n2;
						if ((offset % (100 * (off_t)sizeof(buf))) == 0 && imgsz > 0)
							printf ("\rAppending %s to %s...%d%%", ent2->d_name, ent->d_name,
								(int)(((double)offset / (double)imgsz) * 100));
						fwrite(buf, 1, n2, hFile);
					}
					printf ("\rAppending %s to %s...Done.\n", ent2->d_name, ent->d_name);
				}
				fclose(hFile2);
				if (bRestart)
				{
					snprintf(szPath, sizeof(szPath), "%s/%s", pszDstDir, ent2->d_name);
					unlink(szPath);
					break;
				}
			}
			fclose(hFile);
			if (bRestart)
				break;
		}
	}
	while (bRestart);
	closedir(dir);
	printf ("No more matches found. -> Done\n");
	return true;
}

//---------------------------------------------------------------------------

/* Parses the directory tree to show the names of the recorded movies.
 * It is also able to dump the movies to a destination directory or to just
 * create the destination directory tree based on the directory depending
 * on the given mode setting:
 * To just list the directory, either use MODE_HEUR or MODE_USEFS and set
 * pszDstDir to NULL,
 * To create directory entries, use one of these 2 modes, but also specify
 * pszDstDir.
 * To create the destination directories and extract the movie files into 
 * them, use MODE_EXTRACT and specify pszDstDir.
 *
 * Params:
 *	pszSrc		- Fielname of source image file
 *	pszDstDir	- If creating Dirlist and/or dumping movies, destination
 *				  directory where extracted data should be put to.
 *  dwStart     - Start with this entry of the FNT, normally this should be 0
 *  dwThresh	- Offset of last position where to search directory 
 *				  information on MODE_HEUR.
 *				  Usually you should put MAX_DIRSRCH here.
 *  dwMode		- Mode parameter specifying the action to take. See the
 *				  #defines below for a description.
 * Returns:
 *	TRUE on success, FALSE on failure.
 */
#define MODE_USEFS		1	/* Use Filesystem info instead of dumb scanning */
#define MODE_EXTRACT	2	/* Extract files using Filesytem, requires pszDstDir */
#ifdef FNTENTRY_MAGIC
#define MODE_HEUR		0	/* Heuristic mode, just scan for Dir entries */
#else
#define MODE_HEUR		MODE_USEFS
#endif
#define MODE_MAX		4	/* Maximum mode flag, other hi part is flags */
#define MODE_MASK		(MODE_MAX-1)
#define MODE_FLAG_DUB	128	/* Use "Dubbing" mode, skip deleted chunks and 
							 * merge chunks like edited on recorder. No PTS
							 * updating yet, use with care! */
bool dir(char *pszSrc, char *pszDstDir, DWORD dwStart, DWORD dwThresh, DWORD dwMode)
{
	(void)dwThresh; /* only used when FNTENTRY_MAGIC is defined */
	FILE *fp = NULL, *fpOut = NULL;
	off_t offset = 0;
#ifdef FNTENTRY_MAGIC
	off_t imgsz = 0;
#endif
	int i, kk, kklen, KCurrentProgramNumber;
	FNTENTRY fntentry[FNT_ENTRIES], *pfntentry;
	INODEINDEX InodeIndexTable[1000];
	ADDRESS fntaddr;

	if (pszDstDir)
	{
		struct stat dstst;
		if (stat(pszDstDir, &dstst) != 0 || !S_ISDIR(dstst.st_mode))
		{
			fprintf(stderr, "Cannot write to destination dir %s: %s\n", pszDstDir, strerror(errno));
			return false;
		}
	}

	fp = fopen(pszSrc, "rb");
	if (!fp)
	{
		fprintf(stderr, "Cannot open source file %s: %s\n", pszSrc, strerror(errno));
		return false;
	}

	/* Read FNT address */
	if (!rd_at(fp, (off_t)FNT_OFFSET, &fntaddr, sizeof(fntaddr)))
	{
		fprintf (stderr, "Cannot read FNT address @%X\n", FNT_OFFSET);
		fclose(fp);
		return false;
	}
	offset = (off_t)(fntaddr.block ? B2N_32(fntaddr.block) : 0x4008);
	offset *= (off_t)BLOCK_SIZE;

	/* Get image size (only needed for heuristic scan) */
#ifdef FNTENTRY_MAGIC
	{
		struct stat st;
		if (dwThresh)
			imgsz = (off_t)dwThresh;
		else if (fstat(fileno(fp), &st) == 0)
			imgsz = (off_t)st.st_size;
	}
#endif

	if (fseeko(fp, offset, SEEK_SET) != 0)
	{
		fprintf(stderr, "Cannot seek to FNT @%llx: %s\n", (unsigned long long)offset, strerror(errno));
		fclose(fp);
		return false;
	}

	if (dwMode & MODE_MASK)
	{
		ADDRESS fntdir[FNTDIR_ENTRIES], fatdir[FATDIR_ENTRIES], fotdir[FOTDIR_ENTRIES];
		FATENTRY fatentry[FAT_ENTRIES], *pfatentry;
		FOTENTRY fotentry[FOT_ENTRIES], *pfotentry;
		int j, k, pfx, kpfx, fatoffset, fotoffset, ftoffs, files;

		/* Read a sector (file name table directory) */
		if (fread(&fntdir, 1, sizeof(fntdir), fp) != sizeof(fntdir))
		{
			fprintf (stderr, "Cannot read FNT Directory info @%08llx: %s\n", (unsigned long long)offset, strerror(errno));
			fclose(fp);
			return false;
		}

		/* Read INODE Index table, if needed */
		if ((dwMode & MODE_FLAG_DUB) &&
			fread(&InodeIndexTable, 1, sizeof(InodeIndexTable), fp) != sizeof(InodeIndexTable))
		{
			fprintf (stderr, "Cannot read Inode index table: %s\n", strerror(errno));
			fclose(fp);
			return false;
		}

		/* Go to FATENTRY list */
		offset += (off_t)FAT_OFFSET;
		if (fseeko(fp, offset, SEEK_SET) != 0 ||
			fread(&fatdir, 1, sizeof(fatdir), fp) != sizeof(fatdir))
		{
			fprintf (stderr, "Cannot read FATENTRY list @%08llx: %s\n", (unsigned long long)offset, strerror(errno));
			fclose(fp);
			return false;
		}

		/* Now find block number of file order table */
		offset += (off_t)(FOT_OFFSET - FAT_OFFSET);
		if (fseeko(fp, offset, SEEK_SET) != 0 ||
			fread(&fotdir, 1, sizeof(fotdir), fp) != sizeof(fotdir))
		{
			fprintf (stderr, "Cannot read File order table directory @%08llx: %s\n", (unsigned long long)offset, strerror(errno));
			fclose(fp);
			return false;
		}

		kklen = (dwMode & MODE_FLAG_DUB) ? (int)(sizeof(InodeIndexTable) / sizeof(InodeIndexTable[0])) : 1;

		for (kk = 0; kk < kklen; kk++)
		{
			if ((dwMode & MODE_FLAG_DUB) && !InodeIndexTable[kk])
				break;

			KCurrentProgramNumber = 0;
			for (i = 0, pfx = 0; i < (int)FNTDIR_ENTRIES; i++)
			{
				if (dwMode & MODE_FLAG_DUB)
				{
					if (!fntdir[i].block)
					{
						KCurrentProgramNumber += 8;
						continue;
					}
					kpfx = kk + 1;
				}
				else
				{
					if (!fntdir[i].block)
						break;
					kpfx = pfx;
				}
				offset = (off_t)B2N_32(fntdir[i].block);
				offset *= (off_t)BLOCK_SIZE;
				if (fseeko(fp, offset, SEEK_SET) == 0 &&
					fread(&fntentry, 1, sizeof(fntentry), fp) == sizeof(fntentry))
				{
 				for (j = 0; j < (int)FNT_ENTRIES; j++, pfx++)
					{
						if (!fntentry[j].files && !(dwMode & MODE_FLAG_DUB))
							break;
						if ((DWORD)pfx < dwStart)
							continue;
						KCurrentProgramNumber++;
						if (!(dwMode & MODE_FLAG_DUB) ||
							KCurrentProgramNumber == (int)B2N_16(InodeIndexTable[kk]))
						{
							pfntentry = &fntentry[j];
#ifndef FAT_TS
							printFNTEntry(pszDstDir, kpfx, pfntentry, &pfntentry->ts);
#endif
							files = B2N_16(pfntentry->files);
							if (files > 1)
								printf ("%d files.\n", files);

							fotoffset = (int)B2N_16(pfntentry->fotoffset);
							for (k = 0; k < files && fotoffset; k++)
							{
								fotoffset--;
								if ((ftoffs = fotoffset / (int)FOT_ENTRIES) < (int)FOTDIR_ENTRIES &&
									fotdir[ftoffs].block)
								{
									offset = (off_t)B2N_32(fotdir[ftoffs].block);
									offset *= (off_t)BLOCK_SIZE;
									if (fseeko(fp, offset, SEEK_SET) == 0 &&
										fread(&fotentry, 1, sizeof(fotentry), fp) == sizeof(fotentry))
									{
										pfotentry = &fotentry[fotoffset % (int)FOT_ENTRIES];
										fatoffset = (int)B2N_16(pfotentry->fatoffset) - 1;
										if ((ftoffs = fatoffset / (int)FAT_ENTRIES) < (int)FATDIR_ENTRIES &&
											fatdir[ftoffs].block)
										{
											offset = (off_t)B2N_32(fatdir[ftoffs].block);
											offset *= (off_t)BLOCK_SIZE;
											if (fseeko(fp, offset, SEEK_SET) == 0 &&
												fread(&fatentry, 1, sizeof(fatentry), fp) == sizeof(fatentry))
											{
												pfatentry = &fatentry[fatoffset % FAT_ENTRIES];
#ifdef FAT_TS
												printFNTEntry(pszDstDir, kpfx, pfntentry, &pfatentry->ts);
#endif
												uint32_t filesz = B2N_32(pfatentry->filesz);
												printf ("Size: %u blocks ", filesz);
												uint64_t mb = (uint64_t)filesz * BLOCK_SIZE / 1024 / 1024;
												printf ("(%llu MB)\n", (unsigned long long)mb);

												if (pszDstDir && (dwMode & MODE_MASK) == MODE_EXTRACT)
												{
													char outfile[4096];

													/* Multi-stream: make a ffmpeg concat script */
													if (k > 0)
													{
														snprintf(outfile, sizeof(outfile),
															"%s/[%03d]%s/concat.txt", pszDstDir, kpfx, pfntentry->progname);
														char line[256];
														int len = snprintf(line, sizeof(line),
															"file '%08d.mpg'\n", k);
														fpOut = fopen(outfile, k == 1 ? "w" : "a");
														if (fpOut)
														{
															fwrite(line, 1, (size_t)len, fpOut);
															fclose(fpOut);
														}
														if (k == 1)
														{
															snprintf(outfile, sizeof(outfile),
																"%s/[%03d]%s/concat.sh", pszDstDir, kpfx, pfntentry->progname);
															fpOut = fopen(outfile, "w");
															if (fpOut)
															{
																fprintf(fpOut,
																	"#!/bin/sh\n"
																	"ffmpeg -f concat -safe 0 -i concat.txt -c copy out.vob\n");
																fclose(fpOut);
																chmod(outfile, 0755);
															}
														}
													}

													snprintf(outfile, sizeof(outfile),
														"%s/[%03d]%s/%08d.mpg", pszDstDir, kpfx, pfntentry->progname, k);
													fpOut = fopen(outfile, "wb");
													if (fpOut)
													{
														dumpFile(fp, fpOut, pfatentry);
														fclose(fpOut);

														dumpFNTEntry(pszDstDir, kpfx, pfntentry,
#ifdef FAT_TS
															&pfatentry->ts
#else
															&pfntentry->ts
#endif
															);
													}
													else
													{
														fprintf (stderr, "Cannot write to file %s: %s\n", outfile, strerror(errno));
													}
												}
											}
											else
											{
												fprintf (stderr, "Cannot seek to fatentry #%d @ offset %08llx: %s\n",
													ftoffs, (unsigned long long)offset, strerror(errno));
												break;
											}
										}
										else
											fprintf(stderr, "Invalid FAT entry (#%d)\n", ftoffs);
										fotoffset = B2N_16(pfotentry->nextoffset);
									}
									else
									{
										fprintf (stderr, "Cannot seek to file order table entry #%d @block %X: %s\n",
											ftoffs, B2N_32(fotdir[ftoffs].block), strerror(errno));
										break;
									}
								}
								else
									fprintf(stderr, "Offset %d in FOT doesn't point to a correct directory.\n", fotoffset);
							}
							if (dwMode & MODE_FLAG_DUB)
								break;
						}
					}
				}
				else
				{
					fprintf(stderr, "Cannot read FNTENTRY @%08llx: %s\n", (unsigned long long)offset, strerror(errno));
				}
			}
		}
	}
#ifdef FNTENTRY_MAGIC
	else
	{
		/* Heuristic scan for directory structure */
		while (offset < imgsz)
		{
			if ((offset % (100 * BLOCK_SIZE)) == 0 && imgsz > 0)
				printf ("\r%s %llu/%llu (%d%%)...", "Scanning", (unsigned long long)offset,
					(unsigned long long)imgsz, (int)(((double)offset / (double)imgsz) * 100));

			if (fseeko(fp, offset, SEEK_SET) == 0 &&
				fread(&fntentry, 1, sizeof(fntentry), fp) == sizeof(fntentry))
			{
				for (i = 0; i < (int)FNT_ENTRIES; i++)
				{
					uint16_t first = read_u16(&fntentry[i]);
					if (!first)
						break;
					if (fntentry[i].magic == FNTENTRY_MAGIC)
						printFNTEntry(pszDstDir, i, &fntentry[i], &fntentry[i].ts);
					else
						break;
				}
			}
			offset += (off_t)sizeof(fntentry);
		}
	}
#endif
	fclose(fp);
	printf ("Done.\n");
	return true;
}

/****************************************************************************
 *                                S T A T I C                               *
 ****************************************************************************/

/* Cleans out invalid characters from program name so that a directory with
 * a valid name can be created
 *
 * Params:
 *	pszSpec		- String to clean in place
 * Returns:
 *	1 if any character was changed, 0 otherwise
 */
static int myPathCleanupSpec(char *pszSpec)
{
	int iLen = (int)strlen(pszSpec), o, ret = 0;
	char *p;

	while ((o = (int)strcspn(pszSpec, "/:*?\"<>|")) < iLen)
	{
		switch (pszSpec[o])
		{
		case '/':
			pszSpec[o] = '-'; break;
		case '*':  pszSpec[o] = '+'; break;
		case ':':  pszSpec[o] = ';'; break;
		case '\"': pszSpec[o] = '\''; break;
		case '<':  pszSpec[o] = '('; break;
		case '>':  pszSpec[o] = ')'; break;
		case '|':  pszSpec[o] = 'I'; break;
		default:   pszSpec[o] = '_'; break;
		}
		ret = 1;
	}
	for (p = pszSpec + iLen - 1; p > pszSpec && *p == ' '; p--)
		*p = 0;
	return ret;
}

//---------------------------------------------------------------------------

/* Creates a directory with the correct timestamp from a file name table entry.
 * If the directory already exists, it just sets the timestamp of the
 * directory and all contained files.
 *
 * Params:
 *	pszDstDir	- Directory where the subdirectory will be created/updated
 *	idx			- Number of entry to be dumped. The directory name is prefixed
 *				  with this, as there can be multiple file names in the table
 *				  with the same name.
 *	fnt			- File name table entry.
 *  ts			- Pointer to timestamp that should get used
 * Returns:
 *	-
 */
static void dumpFNTEntry(char *pszDstDir, int idx, FNTENTRY *fnt, TIMESTAMP *ts)
{
	char outfile[4096];
	struct tm tm_time;
	time_t t;

	snprintf(outfile, sizeof(outfile), "%s/[%03d]%s", pszDstDir, idx, fnt->progname);
	mkdir(outfile, 0755);

	/* Build a POSIX time_t from the timestamp */
	memset(&tm_time, 0, sizeof(tm_time));
	tm_time.tm_year = (int)B2N_16(ts->yy) - 1900;
	tm_time.tm_mon  = (int)ts->mm - 1;
	tm_time.tm_mday = (int)ts->dd;
	tm_time.tm_hour = (int)ts->hh;
	tm_time.tm_min  = (int)ts->mi;
	tm_time.tm_sec  = (int)ts->ss;
	tm_time.tm_isdst = -1;

	t = mktime(&tm_time);
	if (t != (time_t)-1)
	{
		struct timespec times[2];
		times[0].tv_sec = t;
		times[0].tv_nsec = UTIME_NOW;
		times[1].tv_sec = t;
		times[1].tv_nsec = UTIME_NOW;

		/* Set directory timestamp */
		utimensat(AT_FDCWD, outfile, times, AT_SYMLINK_NOFOLLOW);

		/* Set timestamp on all files in the directory */
		DIR *d = opendir(outfile);
		if (d)
		{
			struct dirent *ent;
			while ((ent = readdir(d)) != NULL)
			{
				if (ent->d_name[0] == '.')
					continue;
				char filepath[4096];
				(void)snprintf(filepath, sizeof(filepath), "%s/%s", outfile, ent->d_name);
				utimensat(AT_FDCWD, filepath, times, 0);
			}
			closedir(d);
		}
	}
}

//---------------------------------------------------------------------------

/* Prints the specified file name table entry to the console.
 * If pszDstDir is given, also creates a directory with the correct timestamp
 * from the file name table entry in this directory (see dumpFNTentry).
 *
 * Params:
 *	pszDstDir	- Directory where the subdirectory will be created/updated.
 *				  Can be NULL, if directory creation is not needed.
 *	idx			- Number of entry to be dumped. The directory name is prefixed
 *				  with this, as there can be multiple file names in the table
 *				  with the same name.
 *	fnt			- File name table entry.
 *  ts			- Timestamp to show
 * Returns:
 *	-
 */
static void printFNTEntry(char *pszDstDir, int idx, FNTENTRY *fnt, TIMESTAMP *ts)
{
	/* Ensure progname is NUL-terminated */
	fnt->progname[63] = '\0';

	printf ("\r#%03d [%02d.%02d.%04d %02d:%02d:%02d] %s\n",
		idx, ts->dd, ts->mm, B2N_16(ts->yy), ts->hh, ts->mi, ts->ss, fnt->progname);
	if (pszDstDir)
	{
		myPathCleanupSpec(fnt->progname);
		dumpFNTEntry(pszDstDir, idx, fnt, ts);
	}
}

//---------------------------------------------------------------------------

/* Dumps the file at the given FAT entry from image file fp to output
 * file fpout.
 *
 * Params:
 *	fp			- Handle to the image file
 *	fpout		- Handle to the destination file where data will be dumped to.
 *	fat			- File allocation table entry.
 * Returns:
 *	true on success, false on failure.
 */
static bool dumpFile(FILE *fp, FILE *fpOut, FATENTRY *fat)
{
	off_t offset;
	DWORD i, j, k, l, blocks;
	WORD cnk_blocks;
	BYTE buf[BLOCK_SIZE];
	ADDRESS dir[BLOCKDIR_ENTRIES];
	BLISTENTRY chunks[BLOCKDIR_ENTRIES];
	bool bRet = false;

	blocks = B2N_32(fat->filesz);
	offset = (off_t)B2N_32(fat->blockdir.block);
	offset *= (off_t)BLOCK_SIZE;
	if (fseeko(fp, offset, SEEK_SET) == 0 &&
		fread(&dir, 1, sizeof(dir), fp) == sizeof(dir))
	{
		for (i = 0, l = 0; i < BLOCKDIR_ENTRIES; i++)
		{
			if (dir[i].block)
			{
				offset = (off_t)B2N_32(dir[i].block);
				offset *= (off_t)BLOCK_SIZE;
				if (fseeko(fp, offset, SEEK_SET) == 0 &&
					fread(&chunks, 1, sizeof(chunks), fp) == sizeof(chunks))
				{
					for (j = 0; j < BLIST_ENTRIES; j++)
					{
						printf ("\rDumping %u/%u blocks...%d%%", l, blocks,
							(int)(((double)l / (double)blocks) * 100));
						if (chunks[j].block)
						{
							offset = (off_t)B2N_32(chunks[j].block);
							offset *= (off_t)BLOCK_SIZE;
							cnk_blocks = B2N_16(chunks[j].blocks) & 0x7FFF;
							if (fseeko(fp, offset, SEEK_SET) == 0)
							{
								for (k = 0; k < cnk_blocks; k++)
								{
									if (fread(buf, 1, sizeof(buf), fp) == sizeof(buf))
									{
										if (read_u32(buf) != VOB_MAGIC)
											fprintf (stderr, "\rWarning: Block #%u (@%08llx) has no MPEG header!\n",
												k, (unsigned long long)offset);
										if (fwrite(buf, 1, sizeof(buf), fpOut) != sizeof(buf))
										{
											fprintf(stderr, "\r%s: Cannot write block #%u to output file: %s\n",
												"Blocklist", k, strerror(errno));
										}
									}
									else
									{
										fprintf(stderr, "\r%s: Cannot read block #%u: %s\n",
											"Blocklist", k, strerror(errno));
									}
									offset += (off_t)BLOCK_SIZE;
								}
							}
							else
							{
								fprintf(stderr, "\r%s: Cannot seek to offset %08llx: %s\n",
									"Blocklist", (unsigned long long)offset, strerror(errno));
							}
							l += cnk_blocks;
						}
					}
				}
				else
				{
					fprintf(stderr, "\r%s: Cannot seek to offset %08llx: %s\n",
						"Block directory", (unsigned long long)offset, strerror(errno));
				}
			}
		}
		bRet = (l == blocks);
		printf ("\rDumping %u/%u blocks...%s\n", l, blocks, l < blocks ? "Incomplete" : "Done");
	}
	else
	{
		fprintf(stderr, "Cannot seek to offset %08llx: %s\n",
			(unsigned long long)offset, strerror(errno));
	}
	return bRet;
}


/****************************************************************************
 *                                   E I P                                  *
 ****************************************************************************/
int main(int argc, char **argv)
{
	int i;
	DWORD dwXthresh = SCR_THRESH_X, dwMthresh = SCR_THRESH_M, dwStart = 0, dwFlags = 0;
	uint64_t dwOffset = 0;

	printf ("Simple Pioneer " RECORDER " recovery\n(c) by leecher@dose.0wnz.at 2014\n\n");
	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] != '-')
		{
			fprintf(stderr, "You need to specify an action!\n\n");
			break;
		}
		switch (argv[i][1])
		{
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		case '?':
			return usage(argv[0]);
		case 'D':
			dwFlags |= MODE_FLAG_DUB;
			break;
		case 's':
		case 'o':
		case 'c':
			if (argc <= i + 1)
			{
				fprintf(stderr, "%s needs a value!\n", argv[i]);
				return EXIT_FAILURE;
			}
			switch (argv[i][1])
			{
			case 's':
				switch (argv[i][2])
				{
				case 'x': dwXthresh = strtoul(argv[++i], NULL, 0); break;
				case 'm': dwMthresh = strtoul(argv[++i], NULL, 0); break;
				default:
					fprintf(stderr, "-s: You need to set -sx or -sm!\n");
					return EXIT_FAILURE;
				}
				break;
			case 'o':
				dwOffset = strtoull(argv[++i], NULL, 0);
				break;
			case 'c':
				dwStart = strtoul(argv[++i], NULL, 0);
				break;
			}
			break;
		case 'p':
		case 'x':
		case 'd':
		case 'L':
			if (argc <= i + 2)
			{
				fprintf(stderr, "%s needs source file and destination directory!\n", argv[i]);
				return EXIT_FAILURE;
			}
			switch (argv[i][1])
			{
			case 'd':
				return dir(argv[i + 1], argv[i + 2], dwStart, MAX_DIRSRCH, MODE_EXTRACT | dwFlags)
					? EXIT_SUCCESS : EXIT_FAILURE;
			case 'L':
				return dir(argv[i + 1], argv[i + 2], dwStart, MAX_DIRSRCH, MODE_HEUR | dwFlags)
					? EXIT_SUCCESS : EXIT_FAILURE;
			}
			if (!extract(argv[i + 1], argv[i + 2], dwOffset, dwStart, dwXthresh))
				return EXIT_FAILURE;
			if (argv[i][1] != 'p')
				return EXIT_SUCCESS;
			i++;
			__attribute__((fallthrough));
		case 'm':
			if (argc <= i + 1)
			{
				fprintf(stderr, "%s needs destination directory!\n", argv[i]);
				return EXIT_FAILURE;
			}
			return merge(argv[++i], dwMthresh) ? EXIT_SUCCESS : EXIT_FAILURE;
		case 'l':
			if (argc <= i + 1)
			{
				fprintf(stderr, "%s needs source file!\n", argv[i]);
				return EXIT_FAILURE;
			}
			i++;
			if (!dir(argv[i], NULL, dwStart, MAX_DIRSRCH, MODE_USEFS | dwFlags))
			{
#ifdef FNTENTRY_MAGIC
				return dir(argv[i], NULL, dwStart, MAX_DIRSRCH, MODE_HEUR | dwFlags)
					? EXIT_SUCCESS : EXIT_FAILURE;
#else
				return EXIT_FAILURE;
#endif
			}
			return EXIT_SUCCESS;
		default:
			fprintf(stderr, "Unknown commandline option: %s\n\n", argv[i]);
			break;
		}
	}
	return usage(argv[0]);
}
