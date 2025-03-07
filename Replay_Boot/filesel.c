/*--------------------------------------------------------------------
 *                       Replay Firmware
 *                      www.fpgaarcade.com
 *                     All rights reserved.
 *
 *                     admin@fpgaarcade.com
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *--------------------------------------------------------------------
 *
 * Copyright (c) 2020, The FPGAArcade community (see AUTHORS.txt)
 *
 */


#include "filesel.h"
#include "messaging.h"

extern FF_IOMAN* pIoman;

/*
char* GetExtension(char* filename)
{
  uint32_t len = strlen(filename);
  uint32_t i = 0;
  char* pResult = null_string;
  for (i = len-1; i > 0; --i) {
    if (filename[i] == '.')
      return ((char*) filename+i+1);
    if ((filename[i] == '/') || (filename[i] == '\\'))
      break;
  }
  return pResult;
}
*/

int CompareDirEntries(FILEENTRY* pDir1, FILEENTRY* pDir2)
{
    int rc = 0;

    if ( (pDir1->Attrib & FF_FAT_ATTR_DIR) && !(pDir2->Attrib & FF_FAT_ATTR_DIR)) { // directories first
        return -1;
    }

    if (!(pDir1->Attrib & FF_FAT_ATTR_DIR) &&  (pDir2->Attrib & FF_FAT_ATTR_DIR)) { // directories first
        return 1;
    }

    rc = _stricmp_logical(pDir1->FileName, pDir2->FileName);

    // parent dir first (at the same time making sure pDir1 != pDir2)
    if ((pDir1->Attrib & FF_FAT_ATTR_DIR) && !strcmp("..", pDir1->FileName) && rc) {
        return -1;
    }

    if ((pDir2->Attrib & FF_FAT_ATTR_DIR) && !strcmp("..", pDir2->FileName) && rc) {
        return  1;
    }

    if (rc == 0) {
        // if strings are equal, compare case-sensitive
        rc = _strncmp(pDir1->FileName, pDir2->FileName, FF_MAX_FILENAME);
    }

    return (rc);
}

static inline uint8_t FilterExtension(const file_ext_t* file_exts, const char* pFile_ext)
{
    if (!file_exts) {
        return TRUE;
    }

    for (const file_ext_t* ext = file_exts; ext->ext[0] != 0; ++ext) {
        if (ext->ext[0] == '*') {
            return TRUE;
        }

        if (strnicmp(pFile_ext, ext->ext, 3) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}


static inline uint8_t FilterFile(tDirScan* dir_entries, FF_DIRENT* mydir)
{
    uint8_t hide_hidden_entries;              // "hidden" = files/dirs that start with '.'

    if (dir_entries->file_filter[0]) {
        // if we don't have a filter match, we return false
        if (!strcasestr(mydir->FileName, dir_entries->file_filter)) {
            return (FALSE);
        }
    }

    // hidden files/directories stay hidden _always_
    hide_hidden_entries = mydir->FileName[0] == '.' && mydir->FileName[1] != '\0' && mydir->FileName[2] != '\0';

    if (hide_hidden_entries) {
        return FALSE;
    }

    if (mydir->Attrib & FF_FAT_ATTR_DIR) {
        // directories always come through here, except "."
        if (!strcmp(".", mydir->FileName)) {
            return (FALSE);
        }

        // always allow parent (..) dir
        if (!strcmp("..", mydir->FileName)) {
            return (TRUE);
        }

        return (TRUE);
    }

    char* pFile_ext = GetExtension(mydir->FileName);

    if (FilterExtension(dir_entries->file_exts, pFile_ext)) {
        return (TRUE);
    }

    return (FALSE);
}


void PrintSummary(tDirScan* dir_entries)
{
    uint32_t i;

    DEBUG(1, "Filesel : scan first dir:%s ext:%s entries:%lu ", dir_entries->pPath, dir_entries->file_exts ? dir_entries->file_exts->ext : "*", dir_entries->total_entries);
    DEBUG(1, "Filesel : prevc %d nextc %d ", dir_entries->prevc, dir_entries->nextc);

    DEBUG(1, "");
    DEBUG(1, "Summary ");
    DEBUG(1, "");

    if (dir_entries->prevc != 0) {
        for (i = dir_entries->prevc; i > 0; --i) {
            DEBUG(1, " [p %d] %s ", i - 1, dir_entries->dPrev[i - 1].FileName);
        }
    }

    if (dir_entries->nextc != 0) {
        for (i = 0; i < dir_entries->nextc; ++i) {
            DEBUG(1, " [n %d] %s ", i, dir_entries->dNext[i].FileName);
        }
    }
}

void Filesel_ScanUpdate(tDirScan* dir_entries)
{
    FF_DIRENT direntry;
    FILEENTRY mydir;
    int comp_result, comp_result2 = 0;
    FF_ERROR tester = 0;
    uint32_t i, j = 0;

    //ref must be valid
    //finds entries above and below ref
    dir_entries->prevc = 0;
    dir_entries->refc  = 1;
    dir_entries->nextc = 0;

    tester = FF_FindFirst(pIoman, &direntry, dir_entries->pPath); // Find first Object.

    while (tester == 0) {
        if (FilterFile(dir_entries, &direntry)) {

            // Copy info from general representation (FF_DIRENT) to size-minimized (FILENTRY)
            mydir.Attrib = direntry.Attrib;
            strncpy(mydir.FileName, direntry.FileName, sizeof(mydir.FileName));
            mydir.FileName[sizeof(mydir.FileName) - 1] = 0;

            //DEBUG(1,"File %s", mydir.FileName);
            //
            // first check file against our reference
            comp_result = CompareDirEntries(&mydir, &dir_entries->dRef);

            //
            if (comp_result > 0) { // greater than ref

                if (dir_entries->nextc == 0 ) { // first, take it
                    dir_entries->dNext[0] = mydir;
                    dir_entries->nextc++;

                } else {
                    for (i = 0; i < dir_entries->nextc; i++) {
                        comp_result2 = CompareDirEntries(&mydir, &dir_entries->dNext[i]);

                        if (comp_result2 == 0) { // can happen when we scroll and hold onto the first entry
                            break;
                        }

                        //
                        if (comp_result2 < 0) {
                            for (j = dir_entries->nextc; j > i; --j) { // we know entries is 1 or more
                                if (j < MAXDIRENTRIES) { // don't move the last one, it can fall off
                                    dir_entries->dNext[j] = dir_entries->dNext[j - 1];
                                }
                            }

                            dir_entries->dNext[i] = mydir;

                            // add one to count if not full
                            if (dir_entries->nextc < MAXDIRENTRIES) {
                                dir_entries->nextc++;
                            }

                            break; // file is now inserted, move on to next file
                        }

                        // file is bigger than current entry, so we move on to compare with next entry - unless we are at the end
                        if (dir_entries->nextc < MAXDIRENTRIES) {// not full
                            if (i == dir_entries->nextc - 1) { //last
                                dir_entries->dNext[i + 1] = mydir;
                                dir_entries->nextc++;
                                break;
                            }
                        }
                    } // i loop
                }

            } else if (comp_result < 0) { // less than ref, exclude ==0 case
                if (dir_entries->prevc == 0 ) { // first, take it
                    dir_entries->dPrev[0] = mydir;
                    dir_entries->prevc++;

                } else {
                    for (i = 0; i < dir_entries->prevc; i++) {
                        comp_result2 = CompareDirEntries(&mydir, &dir_entries->dPrev[i]);

                        if (comp_result2 == 0) { // can happen when we scroll and hold onto the first entry
                            break;
                        }

                        if (comp_result2 > 0) {
                            for (j = dir_entries->prevc; j > i; --j) { // we know entries is 1 or more
                                if (j < MAXDIRENTRIES) { // don't move the last one, it can fall off
                                    dir_entries->dPrev[j] = dir_entries->dPrev[j - 1];
                                }
                            }

                            dir_entries->dPrev[i] = mydir;

                            // add one to count if not full
                            if (dir_entries->prevc < MAXDIRENTRIES) {
                                dir_entries->prevc++;
                            }

                            break; // file is now inserted, move on to next file
                        }

                        // file is bigger than current entry, so we move on to compare with next entry - unless we are at the end
                        if (dir_entries->prevc < MAXDIRENTRIES) {// not full
                            if (i == dir_entries->prevc - 1) { //last
                                dir_entries->dPrev[i + 1] = mydir;
                                dir_entries->prevc++;
                                break;
                            }
                        }
                    } // i loop
                }
            }
        } // next file

        tester = FF_FindNext(pIoman, &direntry);
    }

    //PrintSummary(dir_entries);
}

// public

void Filesel_ScanFirst(tDirScan* dir_entries)
{

    // mode 0 find first entry *

    FF_DIRENT direntry;
    FILEENTRY mydir;
    int comp_result = 0;
    FF_ERROR tester = 0;
    uint32_t i, j, total = 0;
    // find first entry (lowest)

    dir_entries->prevc = 0;
    dir_entries->refc  = 0;
    dir_entries->nextc = 0; // next entry counter, make sure =0

    dir_entries->offset = 128;
    dir_entries->sel = 129;

    tester = FF_FindFirst(pIoman, &direntry, dir_entries->pPath); // Find first Object.

    while (tester == 0) {
        if (FilterFile(dir_entries, &direntry)) {

            // Copy info from general representation (FF_DIRENT) to size-minimized (FILENTRY)
            mydir.Attrib = direntry.Attrib;
            strncpy(mydir.FileName, direntry.FileName, sizeof(mydir.FileName));
            mydir.FileName[sizeof(mydir.FileName) - 1] = 0;

            //DEBUG(1,"File %s", mydir.FileName);
            //
            if (dir_entries->nextc == 0 ) {
                dir_entries->dNext[0] = mydir;
                dir_entries->nextc++;

            } else {
                for (i = 0; i < dir_entries->nextc; i++) {
                    // compare = 0 is same file, and should be ignored.
                    comp_result = CompareDirEntries(&mydir, &dir_entries->dNext[i]);

                    if (comp_result == 0) { // can happen when we scroll and hold onto the first entry
                        break;
                    }

                    if (comp_result < 0) {

                        //DEBUG(1,"inserted at %d <%s>, moved to %d", i , mydir.FileName, dir_entries->num_entries);
                        for (j = dir_entries->nextc; j > i; --j) { // we know num_entries is 1 or more
                            //DEBUG(1,"J loop %d",j);
                            if (j < MAXDIRENTRIES) { // don't move the last one, it can fall off
                                dir_entries->dNext[j] = dir_entries->dNext[j - 1];
                            }
                        }

                        dir_entries->dNext[i] = mydir;

                        // add one to count if not full
                        if (dir_entries->nextc < MAXDIRENTRIES) {
                            dir_entries->nextc++;
                        }

                        break; // file is now inserted, move on to next file
                    }

                    // file is bigger than current entry, so we move on to compare with next entry - unless we are at the end
                    if (dir_entries->nextc < MAXDIRENTRIES) {// not full
                        if (i == dir_entries->nextc - 1) { //last
                            //DEBUG(1,"Inserted at end, position %d",i+1);
                            dir_entries->dNext[i + 1] = mydir;
                            dir_entries->nextc++;
                            break;
                        }
                    }

                    // end of i loop
                }
            }

            total++; // total file count,
        } // next file

        tester = FF_FindNext(pIoman, &direntry);
    }

    dir_entries->total_entries = total;
    //PrintSummary(dir_entries);
}

// called on startup
void Filesel_Init(tDirScan* dir_entries, char* pPath, const file_ext_t* pExt)
{
    //DEBUG(1,"Init entry, path %s", pPath);

    dir_entries->file_exts = pExt;

    Filesel_ChangeDir(dir_entries, pPath);
}

// called on directory change or startup
void Filesel_ChangeDir(tDirScan* dir_entries, char* pPath)
{
    //DEBUG(1,"ChangeDir entry, path %s", pPath);
    dir_entries->pPath = pPath;
    dir_entries->total_entries = 0;
    dir_entries->prevc = 0;
    dir_entries->nextc = 0;
    dir_entries->refc = 0;
    dir_entries->offset = 128;
    dir_entries->sel = 128;

    dir_entries->file_filter[0] = 0;
}

// called on filter change
void Filesel_AddFilterChar(tDirScan* dir_entries, char letter)
{
    //DEBUG(1,"AddFilterChar entry with '%c'", letter);
    size_t len = strlen(dir_entries->file_filter);

    dir_entries->total_entries = 0;
    dir_entries->prevc = 0;
    dir_entries->nextc = 0;
    dir_entries->refc = 0;
    dir_entries->offset = 128;
    dir_entries->sel = 128;

    if (len < sizeof(dir_entries->file_filter) - 1) {
        dir_entries->file_filter[len] = letter;
        dir_entries->file_filter[len + 1] = 0;
    }
}
void Filesel_DelFilterChar(tDirScan* dir_entries)
{
    //DEBUG(1,"DelFilterChar entry");
    dir_entries->total_entries = 0;
    dir_entries->prevc = 0;
    dir_entries->nextc = 0;
    dir_entries->refc = 0;
    dir_entries->offset = 128;
    dir_entries->sel = 128;

    if (dir_entries->file_filter[0]) {
        size_t len = strlen(dir_entries->file_filter);
        dir_entries->file_filter[len - 1] = 0;
    }
}

void Filesel_ScanFind(tDirScan* dir_entries, uint8_t search)
{
    FF_DIRENT direntry;
    FILEENTRY mydir;
    FILEENTRY dirent_file;
    FILEENTRY dirent_dir;
    FF_ERROR tester = 0;

    uint8_t found_file = 0;
    uint8_t found_dir  = 0;

    // note search is uppercase
    tester = FF_FindFirst(pIoman, &direntry, dir_entries->pPath); // Find first Object.

    while (tester == 0) {
        if (FilterFile(dir_entries, &direntry)) { // returns dirs

            // Copy info from general representation (FF_DIRENT) to size-minimized (FILENTRY)
            mydir.Attrib = direntry.Attrib;
            strncpy(mydir.FileName, direntry.FileName, sizeof(mydir.FileName));

            if (toupper((int)mydir.FileName[0]) == search) {
                if (mydir.Attrib & FF_FAT_ATTR_DIR) { // it's a dir
                    if (found_dir == 0) {
                        dirent_dir = mydir;
                        found_dir++;

                    } else if (CompareDirEntries(&mydir, &dirent_dir) < 0) {
                        dirent_dir = mydir;
                    }

                } else {
                    if (found_file == 0) {
                        dirent_file = mydir;
                        found_file++;

                    } else if (CompareDirEntries(&mydir, &dirent_file) < 0) {
                        dirent_file = mydir;
                    }
                }
            }
        }

        tester = FF_FindNext(pIoman, &direntry);
    }

    // toggle between dir and file selection
    if (Filesel_GetEntry(dir_entries, dir_entries->sel).Attrib & FF_FAT_ATTR_DIR) {
        // currently on a directory, choose a file if possible
        if (found_file != 0) {
            mydir = dirent_file;

        } else if (found_dir != 0) {
            mydir = dirent_dir;
        }

    } else { // on a file, choose a dir if possible
        if (found_dir != 0) {
            mydir = dirent_dir;

        } else if (found_file != 0) {
            mydir = dirent_file;
        }
    }

    // jump
    if (found_file != 0 || found_dir != 0) {
        dir_entries->dRef = mydir;
        Filesel_ScanUpdate(dir_entries);
        dir_entries->offset = 127;
        dir_entries->sel = 128;
    }
}

FILEENTRY Filesel_GetEntry(tDirScan* dir_entries, uint8_t entry)
{
    uint8_t offset = 0;

    if (entry == 128) {
        if (dir_entries->refc != 0) {
            return dir_entries->dRef;
        }

    } else if (entry < 128) {
        offset = 127 - entry;

        if (offset < dir_entries->prevc) {
            return dir_entries->dPrev[offset];
        }

    } else if (entry > 128) {
        offset = entry - 129;

        if (offset < dir_entries->nextc) {
            return dir_entries->dNext[offset];
        }
    }

    FILEENTRY dNull;
    memset(&dNull, 0x00, sizeof(dNull));

    return dNull;
}

FILEENTRY Filesel_GetLine(tDirScan* dir_entries, uint8_t pos)
{
    return Filesel_GetEntry(dir_entries, dir_entries->offset + pos);
}

uint8_t Filesel_GetSel(tDirScan* dir_entries)
{
    // offset points at the display base, this will be line 0
    return dir_entries->sel - dir_entries->offset;
}


static uint8_t Filesel_CheckIndex(tDirScan* dir_entries, uint8_t offset)
{
    // 1 is ok
    if (offset == 128) {
        if (dir_entries->refc != 0) {
            return 1;
        }

    } else if (offset < 128) {
        if ((127 - offset) < dir_entries->prevc) {
            return 1;
        }

    } else if (offset > 128) {
        if ((offset - 129) < dir_entries->nextc) {
            return 1;
        }
    }

    return 0;
}

uint8_t Filesel_Update(tDirScan* dir_entries, uint8_t opt)
{
    // if step up/down and < max then we are at the end
    // if about to go off screen rescan
    //(check_next())
    uint8_t current = (dir_entries->sel - dir_entries->offset);
    uint8_t sel = 0;

    if (opt == SCAN_NEXT) {
        sel = dir_entries->sel + 1;

        if (Filesel_CheckIndex(dir_entries, sel)) {
            /*if (current >= 6) {*/
            if (current >= MAXDIRENTRIES - 1) {
                // take current index as ref
                dir_entries->dRef = Filesel_GetEntry(dir_entries, dir_entries->sel);
                Filesel_ScanUpdate(dir_entries);
                dir_entries->offset = 128;
                dir_entries->sel = 129;
                return SCAN_OK;
            }

            dir_entries->sel = sel;
            return SCAN_OK;
        }

        return SCAN_END;


    } else if (opt == SCAN_PREV) {
        sel = dir_entries->sel - 1;

        if (Filesel_CheckIndex(dir_entries, sel)) {
            if (current <= 1) {
                // take current index as ref
                dir_entries->dRef = Filesel_GetEntry(dir_entries, dir_entries->sel);
                Filesel_ScanUpdate(dir_entries);
                dir_entries->offset = 128 - MAXDIRENTRIES;
                dir_entries->sel = 127;
                return SCAN_OK;
            }

            dir_entries->sel = sel;
            return SCAN_OK;
        }

        return SCAN_END;

    } else if (opt == SCAN_NEXT_PAGE) {
        // find last visible
        for (sel = dir_entries->offset + MAXDIRENTRIES; sel >= dir_entries->offset; --sel) {
            if (Filesel_CheckIndex(dir_entries, sel)) {
                dir_entries->dRef = Filesel_GetEntry(dir_entries, sel);
                Filesel_ScanUpdate(dir_entries);
                current = (sel - dir_entries->offset);

                if (current == MAXDIRENTRIES) {
                    current = 1;
                }

                dir_entries->offset = 128 - current;
                dir_entries->sel = 128;

                return SCAN_OK;
            }
        }

        return SCAN_END;

    } else if (opt == SCAN_PREV_PAGE) {
        // find first visible
        for (sel = dir_entries->offset; sel < dir_entries->offset + MAXDIRENTRIES + 1; ++sel) {
            if (Filesel_CheckIndex(dir_entries, sel)) {
                dir_entries->dRef = Filesel_GetEntry(dir_entries, sel);
                Filesel_ScanUpdate(dir_entries);
                current = (sel - dir_entries->offset);

                if (current == 0) {
                    current = MAXDIRENTRIES - 1;
                }

                dir_entries->offset = 128 - current;
                dir_entries->sel = 128;

                return SCAN_OK;
            }
        }

        return SCAN_END;

    }

    return SCAN_OK;
}

