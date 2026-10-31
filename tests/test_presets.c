/*
 * test_presets.c
 *
 * Host test of the preset manager's memory accesses (task E4.7): the project
 * re-save (empty_directory() in Core/Src/Preset/presetManager.c), and the
 * kit-slot and project numbers that come off the card or in by MIDI. fw 1.7
 * wrote and read past the 13-byte currentFolder, the 64 kits of mainParameters
 * and the 64 slots of savefileNames for them. Also the active step that NRPN 121
 * sets (frontParser_parseNrpn() in Core/Src/frontPanelParser.c, task E4.8): fw 1.7
 * took 0-127 for the 64 steps, and the step page wrote pattern data past them.
 * And the morph flag of stage 1b's F10: preset_morphBusy() is 1 while
 * preset_morph_tick() still has sound parameters to send.
 *
 * presetManager.c and Core/Src/frontPanelParser.c are compiled as they are,
 * against the stand-in headers in tests/stubs/presets/ and tests/stubs/, the
 * firmware's FatFs header with its configuration, and the stand-in functions
 * below. The card is an in-memory one: a table of files and folders, with the
 * FatFs calls the preset manager makes. Three names are changed for this target
 * (tests/CMakeLists.txt): currentFolder, savefileNames and mainParameters become
 * pointers to arrays of their size, which the test points into larger buffers.
 * The bytes past each array are guard bytes: an access past it lands there, also
 * without a sanitizer, and the test sees it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include <stddef.h>

#include "frontPanelParser_ui.h"
#include "lcd.h"
#include "menu/CcNr2Text.h"
#include "menu/menu.h"
#include "Preset/PresetManager.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Soundengine.h"
#include "buttonHandler.h"
#include "IO/uart.h"
#include "ledHandler.h"

#define GUARD_BYTE 0xA5
#define FOLDER_SIZE 13
#define FOLDER_GUARD 51
#define NAME_SLOT_SIZE (NUM_SAVEFILE_NAMES * 8)
#define KIT_SIZE END_OF_SOUND_PARAMETERS
#define SLOTS_WITH_GUARD 256	/* a slot byte is a uint8_t: 64 slots and 192 guard slots */

_Static_assert(sizeof currentFolder == FOLDER_SIZE, "currentFolder of presetManager.c");
_Static_assert(sizeof savefileNames == NUM_KIT_SLOTS * NAME_SLOT_SIZE, "savefileNames of presetManager.c");
_Static_assert(sizeof mainParameters == NUM_KIT_SLOTS * KIT_SIZE, "mainParameters of menu.h");

//------------------------------------------------------------------------------
// The arrays and their guard bytes
//------------------------------------------------------------------------------
static char folderMemory[FOLDER_SIZE + FOLDER_GUARD];
static char namesMemory[SLOTS_WITH_GUARD * NAME_SLOT_SIZE];
static uint8_t kitsMemory[SLOTS_WITH_GUARD * KIT_SIZE];

/* mainParameters is defined in menu.c on the device; here it is the test's */
uint8_t (*preset_mainTable)[NUM_KIT_SLOTS][END_OF_SOUND_PARAMETERS];

/** guard bytes past the end of the first size bytes of memory that changed */
static long changedPast(const void *memory, size_t size, size_t total)
{
	const uint8_t *m = memory;
	long n = 0;
	for (size_t i = size; i < total; i++)
		if (m[i] != GUARD_BYTE)
			n++;
	return n;
}

static long folderGuardChanged(void) { return changedPast(folderMemory, FOLDER_SIZE, sizeof folderMemory); }
static long namesGuardChanged(void) { return changedPast(namesMemory, sizeof savefileNames, sizeof namesMemory); }
static long kitsGuardChanged(void) { return changedPast(kitsMemory, sizeof mainParameters, sizeof kitsMemory); }

//------------------------------------------------------------------------------
// The in-memory card
//------------------------------------------------------------------------------
#define CARD_ENTRIES 320
#define CARD_PATH 80

typedef struct {
	int used;
	int isDir;
	char path[CARD_PATH];	/* absolute, without a trailing '/' */
	uint8_t *data;
	UINT len;
} CardEntry;

static CardEntry card[CARD_ENTRIES];
static char cardCwd[CARD_PATH] = "/";
static int cardOpenFile = -1;

static int lowerChar(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

/** FatFs 8.3 names are not case sensitive */
static int sameName(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if (lowerChar((unsigned char)*a) != lowerChar((unsigned char)*b))
			return 0;
	return *a == *b;
}

/** '*' and '?' as f_findfirst() takes them */
static int matches(const char *pattern, const char *name)
{
	if (*pattern == 0)
		return *name == 0;
	if (*pattern == '*')
		return matches(pattern + 1, name) || (*name && matches(pattern, name + 1));
	if (*name && (*pattern == '?' || lowerChar((unsigned char)*pattern) == lowerChar((unsigned char)*name)))
		return matches(pattern + 1, name + 1);
	return 0;
}

/** path as an absolute one, cut at CARD_PATH - 1 characters */
static void absolutePath(char *out, const char *path)
{
	size_t n = 0;
	if (path[0] != '/') {
		for (const char *c = cardCwd; *c && n < CARD_PATH - 1; c++)
			out[n++] = *c;
		if (strcmp(cardCwd, "/") != 0 && n < CARD_PATH - 1)
			out[n++] = '/';
	}
	for (const char *c = path; *c && n < CARD_PATH - 1; c++)
		out[n++] = *c;
	out[n] = 0;
}

static int findAbsolute(const char *abs)
{
	for (int i = 0; i < CARD_ENTRIES; i++)
		if (card[i].used && sameName(card[i].path, abs))
			return i;
	return -1;
}

static int find(const char *path)
{
	char abs[CARD_PATH];
	absolutePath(abs, path);
	return findAbsolute(abs);
}

/** the name of entry i if it is in folder dir, or NULL */
static const char *nameIn(int i, const char *dir)
{
	const char *slash = strrchr(card[i].path, '/');
	size_t dirLen = (size_t)(slash - card[i].path);
	if (strcmp(dir, "/") == 0)
		return dirLen == 0 ? slash + 1 : NULL;
	if (dirLen != strlen(dir))
		return NULL;
	for (size_t k = 0; k < dirLen; k++)
		if (lowerChar((unsigned char)card[i].path[k]) != lowerChar((unsigned char)dir[k]))
			return NULL;
	return slash + 1;
}

static int addEntry(const char *path, int isDir)
{
	for (int i = 0; i < CARD_ENTRIES; i++)
		if (!card[i].used) {
			card[i] = (CardEntry){ .used = 1, .isDir = isDir };
			absolutePath(card[i].path, path);
			return i;
		}
	printf("the in-memory card is full\n");
	exit(1);
}

static void removeEntry(int i)
{
	free(card[i].data);
	card[i] = (CardEntry){ 0 };
}

static void cardReset(void)
{
	for (int i = 0; i < CARD_ENTRIES; i++)
		removeEntry(i);
	snprintf(cardCwd, sizeof cardCwd, "/");
	cardOpenFile = -1;
}

/** a file with the given bytes (path absolute) */
static void cardPut(const char *path, const void *data, UINT len)
{
	int i = findAbsolute(path);
	if (i < 0)
		i = addEntry(path, 0);
	free(card[i].data);
	card[i].data = malloc(len ? len : 1);
	if (len)
		memcpy(card[i].data, data, len);
	card[i].len = len;
}

static int cardHas(const char *path) { return findAbsolute(path) >= 0; }

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode)
{
	int i = find(path);
	if (mode & (FA_CREATE_ALWAYS | FA_CREATE_NEW | FA_OPEN_ALWAYS)) {
		if (i < 0)
			i = addEntry(path, 0);
		if (mode & FA_CREATE_ALWAYS) {
			free(card[i].data);
			card[i].data = NULL;
			card[i].len = 0;
		}
	}
	if (i < 0 || card[i].isDir)
		return FR_NO_FILE;
	cardOpenFile = i;
	fp->fptr = 0;
	return FR_OK;
}

FRESULT f_close(FIL *fp)
{
	(void)fp;
	cardOpenFile = -1;
	return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
	*br = 0;
	if (cardOpenFile < 0)
		return FR_INVALID_OBJECT;
	const CardEntry *e = &card[cardOpenFile];
	UINT n = fp->fptr < e->len ? e->len - (UINT)fp->fptr : 0;
	if (n > btr)
		n = btr;
	if (n)
		memcpy(buff, e->data + fp->fptr, n);
	fp->fptr += n;
	*br = n;
	return FR_OK;
}

FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
	*bw = 0;
	if (cardOpenFile < 0)
		return FR_INVALID_OBJECT;
	CardEntry *e = &card[cardOpenFile];
	UINT end = (UINT)fp->fptr + btw;
	if (end > e->len) {
		e->data = realloc(e->data, end);
		memset(e->data + e->len, 0, end - e->len);
		e->len = end;
	}
	if (btw)
		memcpy(e->data + fp->fptr, buff, btw);
	fp->fptr = end;
	*bw = btw;
	return FR_OK;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs)
{
	fp->fptr = ofs;
	return FR_OK;
}

FRESULT f_stat(const TCHAR *path, FILINFO *fno)
{
	int i = find(path);
	if (i < 0)
		return FR_NO_FILE;
	memset(fno, 0, sizeof *fno);
	fno->fattrib = card[i].isDir ? AM_DIR : 0;
	fno->fsize = card[i].len;
	snprintf(fno->fname, sizeof fno->fname, "%s", strrchr(card[i].path, '/') + 1);
	return FR_OK;
}

FRESULT f_mkdir(const TCHAR *path)
{
	if (find(path) >= 0)
		return FR_EXIST;
	addEntry(path, 1);
	return FR_OK;
}

FRESULT f_chdir(const TCHAR *path)
{
	char abs[CARD_PATH];
	absolutePath(abs, path);
	if (strcmp(abs, "/") != 0) {
		int i = findAbsolute(abs);
		if (i < 0 || !card[i].isDir)
			return FR_NO_PATH;
	}
	snprintf(cardCwd, sizeof cardCwd, "%s", abs);
	return FR_OK;
}

FRESULT f_unlink(const TCHAR *path)
{
	int i = find(path);
	if (i < 0)
		return FR_NO_FILE;
	if (card[i].isDir)
		for (int k = 0; k < CARD_ENTRIES; k++)
			if (card[k].used && nameIn(k, card[i].path))
				return FR_DENIED;
	removeEntry(i);
	return FR_OK;
}

/* A folder being read: dir.clust is the index of its card entry (CARD_ENTRIES
 * for the root), dir.dptr the next entry to look at. */
static const char *dirPath(const DIR *dp)
{
	return dp->clust == CARD_ENTRIES ? "/" : card[dp->clust].path;
}

FRESULT f_opendir(DIR *dp, const TCHAR *path)
{
	char abs[CARD_PATH];
	absolutePath(abs, path);
	memset(dp, 0, sizeof *dp);
	if (strcmp(abs, "/") == 0) {
		dp->clust = CARD_ENTRIES;
		return FR_OK;
	}
	int i = findAbsolute(abs);
	if (i < 0 || !card[i].isDir)
		return FR_NO_PATH;
	dp->clust = (DWORD)i;
	return FR_OK;
}

FRESULT f_closedir(DIR *dp)
{
	(void)dp;
	return FR_OK;
}

FRESULT f_readdir(DIR *dp, FILINFO *fno)
{
	memset(fno, 0, sizeof *fno);
	for (; dp->dptr < CARD_ENTRIES; dp->dptr++) {
		const int i = (int)dp->dptr;
		const char *name = card[i].used ? nameIn(i, dirPath(dp)) : NULL;
		if (name) {
			snprintf(fno->fname, sizeof fno->fname, "%s", name);
			fno->fattrib = card[i].isDir ? AM_DIR : 0;
			fno->fsize = card[i].len;
			dp->dptr++;
			return FR_OK;
		}
	}
	return FR_OK;	/* fname[0] == 0: the end of the folder */
}

FRESULT f_findfirst(DIR *dp, FILINFO *fno, const TCHAR *path, const TCHAR *pattern)
{
	FRESULT res = f_opendir(dp, path[0] ? path : cardCwd);
	if (res != FR_OK)
		return res;
	do
		res = f_readdir(dp, fno);
	while (res == FR_OK && fno->fname[0] && !matches(pattern, fno->fname));
	return res;
}

//------------------------------------------------------------------------------
// The state of each test
//------------------------------------------------------------------------------
/** the firmware state, with guard bytes past the arrays; the card is kept */
static void resetMemory(void)
{
	memset(folderMemory, GUARD_BYTE, sizeof folderMemory);
	memset(namesMemory, GUARD_BYTE, sizeof namesMemory);
	memset(kitsMemory, GUARD_BYTE, sizeof kitsMemory);
	preset_folderBuf = (char (*)[FOLDER_SIZE])folderMemory;
	preset_namesTable = (char (*)[NUM_KIT_SLOTS][NUM_SAVEFILE_NAMES][8])namesMemory;
	preset_mainTable = (uint8_t (*)[NUM_KIT_SLOTS][END_OF_SOUND_PARAMETERS])kitsMemory;
	memset(currentFolder, 0, sizeof currentFolder);
	memset(savefileNames, 0, sizeof savefileNames);
	for (int k = 0; k < NUM_KIT_SLOTS; k++)
		memset(mainParameters[k], k, KIT_SIZE);
	morphParameters = mainParameters[0];
	memset(activeParameters, 0, sizeof activeParameters);
	memset(menu_selectedPresetNr, 0, sizeof menu_selectedPresetNr);
	memset(menu_loadedPresetNr, 0, sizeof menu_loadedPresetNr);
	memset(&seq_patternSet, 0, sizeof seq_patternSet);
	seq_activePattern = seq_pendingPattern = 0;
	menu_playedPattern = menu_shownPattern = 0;
	lcd_process_display_interrupt = 1;
}

/** an empty card with the /tmp folder, and resetMemory() */
static void reset(void)
{
	cardReset();
	addEntry("/tmp", 1);
	resetMemory();
}

//------------------------------------------------------------------------------
// The tests
//------------------------------------------------------------------------------
TEST(presets_project_resave_stays_in_currentFolder)
{
	/* Save a project into a folder that exists: the folder is emptied first,
	 * through a path of "/proj00/" and each 8.3 name of the folder. */
	reset();
	snprintf(savefileNames[0][KIT_NAMES], 8, "INITK");
	snprintf(savefileNames[0][PROJECT_NAMES], 8, "MYPROJ");
	addEntry("/proj00", 1);
	cardPut("/proj00/00-INITK.SND", "old", 3);
	cardPut("/proj00/12-ABCDE.PAT", "old", 3);
	cardPut("/proj00/OLD.TXT", "old", 3);

	ASSERT_EQ(preset_saveProject(0), FR_OK);
	ASSERT_EQ(folderGuardChanged(), 0);
	ASSERT_EQ(namesGuardChanged(), 0);
	ASSERT_EQ(kitsGuardChanged(), 0);
	ASSERT_TRUE(strcmp(currentFolder, "/proj00") == 0);
	ASSERT_TRUE(strcmp(savefileNames[0][KIT_NAMES], "INITK") == 0);
	/* the folder was emptied and saved again */
	ASSERT_FALSE(cardHas("/proj00/OLD.TXT"));
	ASSERT_FALSE(cardHas("/proj00/12-ABCDE.PAT"));
	ASSERT_TRUE(cardHas("/proj00/00-INITK.snd"));
	ASSERT_TRUE(cardHas("/proj00/prj.nfo"));
	ASSERT_TRUE(cardHas("/proj00/63-.sng"));
	ASSERT_EQ(lcd_process_display_interrupt, 1);
}

TEST(presets_project_resave_empties_a_subfolder)
{
	reset();
	addEntry("/proj01", 1);
	addEntry("/proj01/SUBDIR01.ABC", 1);
	cardPut("/proj01/SUBDIR01.ABC/FILENAME.EXT", "old", 3);
	ASSERT_EQ(preset_saveProject(1), FR_OK);
	ASSERT_EQ(folderGuardChanged(), 0);
	ASSERT_FALSE(cardHas("/proj01/SUBDIR01.ABC/FILENAME.EXT"));
	ASSERT_FALSE(cardHas("/proj01/SUBDIR01.ABC"));
	ASSERT_TRUE(cardHas("/proj01/prj.nfo"));
}

TEST(presets_project_resave_stops_at_a_path_too_long)
{
	/* "/proj02" and five levels of 12-character names: 72 characters, which
	 * the path buffer of the re-save does not hold */
	reset();
	addEntry("/proj02", 1);
	addEntry("/proj02/AAAAAAAA.AAA", 1);
	addEntry("/proj02/AAAAAAAA.AAA/BBBBBBBB.BBB", 1);
	addEntry("/proj02/AAAAAAAA.AAA/BBBBBBBB.BBB/CCCCCCCC.CCC", 1);
	addEntry("/proj02/AAAAAAAA.AAA/BBBBBBBB.BBB/CCCCCCCC.CCC/DDDDDDDD.DDD", 1);
	cardPut("/proj02/AAAAAAAA.AAA/BBBBBBBB.BBB/CCCCCCCC.CCC/DDDDDDDD.DDD/EEEEEEEE.EEE", "old", 3);
	ASSERT_EQ(preset_saveProject(2), FR_INVALID_NAME);
	ASSERT_EQ(folderGuardChanged(), 0);
	ASSERT_TRUE(cardHas("/proj02/AAAAAAAA.AAA/BBBBBBBB.BBB/CCCCCCCC.CCC/DDDDDDDD.DDD/EEEEEEEE.EEE"));
	ASSERT_EQ(lcd_process_display_interrupt, 1);
}

TEST(presets_project_first_save_makes_the_folder)
{
	reset();
	ASSERT_EQ(preset_saveProject(5), FR_OK);
	ASSERT_EQ(folderGuardChanged(), 0);
	ASSERT_TRUE(cardHas("/proj05/prj.nfo"));
	ASSERT_TRUE(cardHas("/proj05/00-.snd"));
}

/* in presetManager.c, not in PresetManager.h */
FRESULT preset_saveTmpKit(void);
FRESULT preset_saveTmpPat(void);

/** bytes of activeParameters (the sound parameters) that differ from value */
static int activeDiffers(uint8_t value)
{
	int n = 0;
	for (int i = 0; i < KIT_SIZE; i++)
		if (activeParameters[i] != value)
			n++;
	return n;
}

TEST(presets_kit_ram_load_stays_in_the_64_kits)
{
	for (int slot = NUM_KIT_SLOTS; slot < 256; slot++) {
		reset();
		memset(activeParameters, 0x3C, KIT_SIZE);
		preset_loadDrumsetRam((uint8_t)slot, 0, 0);
		preset_loadDrumsetRam((uint8_t)slot, 0, 1);
		ASSERT_EQ(activeDiffers(0x3C), 0);
		preset_loadDrumsetRam((uint8_t)slot, 1, 0);
		ASSERT_TRUE(morphParameters == mainParameters[0]);
	}
	/* a kit in range is loaded as before (sendDrumsetParameters() puts the
	 * six LFO voice numbers of the kit, 17, back to 1). LICHOMAT v0.4: the
	 * live DJ filter and freeze keep their values, 0 here (plan D33) */
	reset();
	preset_loadDrumsetRam(17, 0, 0);
	ASSERT_EQ(activeDiffers(17), 6 + 2);
	ASSERT_EQ(activeParameters[PAR_DJ_FILTER], 0);
	ASSERT_EQ(activeParameters[PAR_REV_FREEZE], 0);
	ASSERT_EQ(activeParameters[PAR_VOICE_LFO1], 1);
	preset_loadDrumsetRam(63, 1, 0);
	ASSERT_TRUE(morphParameters == mainParameters[63]);
}

TEST(presets_kit_ram_save_stays_in_the_64_kits)
{
	for (int slot = NUM_KIT_SLOTS; slot < 256; slot++) {
		reset();
		memset(activeParameters, 0x3C, KIT_SIZE);
		preset_saveDrumsetRam((uint8_t)slot, 0);
		preset_saveDrumsetRam((uint8_t)slot, 1);
		ASSERT_EQ(kitsGuardChanged(), 0);
	}
	reset();
	memset(activeParameters, 0x3C, KIT_SIZE);
	preset_saveDrumsetRam(9, 0);
	ASSERT_TRUE(memcmp(mainParameters[9], activeParameters, KIT_SIZE) == 0);
	ASSERT_EQ(mainParameters[8][0], 8);
	ASSERT_EQ(mainParameters[10][0], 10);
}

/** tmp.snd as preset_saveTmpKit() writes it: the kit slot, then the kit */
static void putTmpKit(uint8_t slot)
{
	uint8_t file[1 + KIT_SIZE];
	file[0] = slot;
	memset(file + 1, 0x22, KIT_SIZE);
	cardPut("/tmp/tmp.snd", file, sizeof file);
	ASSERT_EQ(f_chdir("/tmp"), FR_OK);
}

TEST(presets_tmp_kit_slot_is_checked)
{
	for (int slot = NUM_KIT_SLOTS; slot < 256; slot++) {
		reset();
		putTmpKit((uint8_t)slot);
		ASSERT_EQ(preset_LoadTmpKit(), FR_OK);
		ASSERT_EQ(menu_selectedPresetNr[SAVE_TYPE_KIT], 0);
		ASSERT_EQ(activeParameters[0], 0x22);	/* the kit itself is loaded */
	}
	reset();
	putTmpKit(63);
	ASSERT_EQ(preset_LoadTmpKit(), FR_OK);
	ASSERT_EQ(menu_selectedPresetNr[SAVE_TYPE_KIT], 63);
	/* the autosave writes the slot back as it is */
	ASSERT_EQ(preset_saveTmpKit(), FR_OK);
	ASSERT_EQ(card[find("/tmp/tmp.snd")].data[0], 63);
}

TEST(presets_pattern_file_kit_slot_is_checked)
{
	const uint8_t slots[] = { 0, 42, 63, 64, 128, 255 };
	for (unsigned k = 0; k < sizeof slots; k++) {
		reset();
		ASSERT_EQ(f_mkdir("/proj00"), FR_OK);
		ASSERT_EQ(f_chdir("/proj00"), FR_OK);
		seq_patternSet.kitNr[5] = slots[k];
		ASSERT_EQ(preset_savePatternSD(5), FR_OK);
		seq_patternSet.kitNr[5] = 7;
		ASSERT_EQ(preset_loadPatternSD(5), FR_OK);
		ASSERT_EQ(seq_patternSet.kitNr[5], slots[k] < NUM_KIT_SLOTS ? slots[k] : 0);
	}
}

TEST(presets_tmp_pattern_kit_slot_is_checked)
{
	const uint8_t slots[] = { 0, 42, 63, 64, 128, 255 };
	for (unsigned k = 0; k < sizeof slots; k++) {
		reset();
		ASSERT_EQ(f_chdir("/tmp"), FR_OK);
		menu_playedPattern = 3;
		seq_patternSet.kitNr[3] = slots[k];
		ASSERT_EQ(preset_saveTmpPat(), FR_OK);
		seq_patternSet.kitNr[3] = 7;
		ASSERT_EQ(preset_LoadTmpPat(), FR_OK);
		ASSERT_EQ(seq_patternSet.kitNr[3], slots[k] < NUM_KIT_SLOTS ? slots[k] : 0);
	}
}

TEST(presets_project_load_autoloads_a_kit_in_range)
{
	/* a project whose first pattern names kit 255, loaded with the kit
	 * autoload on: the boot path of a corrupt or foreign card */
	reset();
	activeParameters[PAR_PATTERN_MODE] = 1;
	seq_patternSet.kitNr[0] = 255;
	ASSERT_EQ(preset_saveProject(0), FR_OK);
	resetMemory();
	memset(activeParameters, 0x3C, KIT_SIZE);
	ASSERT_EQ(preset_loadProject(0, 0), FR_OK);
	ASSERT_EQ(activeParameters[PAR_PATTERN_MODE], 1);
	ASSERT_EQ(seq_patternSet.kitNr[0], 0);
	ASSERT_EQ(menu_selectedPresetNr[SAVE_TYPE_KIT], 0);
	ASSERT_EQ(menu_loadedPresetNr[CURRENT_KIT], 0);
	ASSERT_EQ(kitsGuardChanged(), 0);
	ASSERT_EQ(namesGuardChanged(), 0);
}

TEST(presets_last_project_number_is_checked)
{
	for (int nr = 0; nr < 256; nr++) {
		reset();
		const uint8_t file = (uint8_t)nr;
		cardPut("/proj.idx", &file, 1);
		uint8_t projNr = 0xEE;
		ASSERT_EQ(preset_loadLastUsedProjectNrFromSD(&projNr), FR_OK);
		ASSERT_EQ(projNr, nr < NUM_KIT_SLOTS ? nr : 0);
	}
	/* no file: project 0, as before */
	reset();
	uint8_t projNr = 0xEE;
	ASSERT_EQ(preset_loadLastUsedProjectNrFromSD(&projNr), FR_NO_FILE);
	ASSERT_EQ(projNr, 0);
}

TEST(presets_project_save_past_the_64_slots_is_refused)
{
	const uint8_t slots[] = { 64, 99, 100, 255 };
	for (unsigned k = 0; k < sizeof slots; k++) {
		char folder[16];
		snprintf(folder, sizeof folder, "/proj%02d", slots[k]);
		reset();
		memset(activeParameters, 0x3C, KIT_SIZE);
		ASSERT_EQ(preset_saveProject(slots[k]), FR_INVALID_PARAMETER);
		ASSERT_FALSE(cardHas(folder));
		ASSERT_EQ(namesGuardChanged(), 0);
		ASSERT_EQ(kitsGuardChanged(), 0);
		ASSERT_EQ(mainParameters[0][0], 0);	/* the edit buffer was not stored */
		ASSERT_EQ(lcd_process_display_interrupt, 1);
	}
}

TEST(presets_project_load_past_the_64_slots_is_refused)
{
	const uint8_t slots[] = { 64, 99, 100, 255 };
	for (unsigned k = 0; k < sizeof slots; k++) {
		char folder[16], nfo[32];
		snprintf(folder, sizeof folder, "/proj%02d", slots[k]);
		snprintf(nfo, sizeof nfo, "%s/prj.nfo", folder);
		reset();
		addEntry(folder, 1);
		cardPut(nfo, "STRAY   ", 8);
		ASSERT_EQ(preset_loadProject(slots[k], 0), FR_INVALID_PARAMETER);
		ASSERT_EQ(namesGuardChanged(), 0);
		ASSERT_EQ(lcd_process_display_interrupt, 1);
		/* and no folder is made for a slot that has none */
		const uint8_t other = (uint8_t)(slots[k] ^ 1);
		snprintf(folder, sizeof folder, "/proj%02d", other);
		ASSERT_EQ(preset_loadProject(other, 0), FR_INVALID_PARAMETER);
		ASSERT_FALSE(cardHas(folder));
	}
	/* a project in range loads as before */
	reset();
	snprintf(savefileNames[63][PROJECT_NAMES], 8, "LAST");
	ASSERT_EQ(preset_saveProject(63), FR_OK);
	resetMemory();
	ASSERT_EQ(preset_loadProject(63, 0), FR_OK);
	ASSERT_TRUE(memcmp(savefileNames[63][PROJECT_NAMES], "LAST", 5) == 0);
	ASSERT_EQ(namesGuardChanged(), 0);
}

/* in frontPanelParser.c and presetManager.c, not in their headers */
void frontPanel_ccHandler(void);
extern uint8_t preset_kitDirty;

/** CC 0 (Bank Select MSB) as the front panel parser hands it on: parameter 255 */
static void bankSelect(uint8_t value)
{
	frontParser_midiMsg.status = MIDI_CC;
	frontParser_midiMsg.data1 = 0;
	frontParser_midiMsg.data2 = value;
	frontPanel_ccHandler();
}

TEST(presets_bank_select_past_the_64_kits_is_ignored)
{
	for (int v = NUM_KIT_SLOTS; v < 256; v++) {
		reset();
		preset_kitDirty = 0;
		menu_selectedPresetNr[SAVE_TYPE_KIT] = 5;
		memset(activeParameters, 0x3C, KIT_SIZE);
		bankSelect((uint8_t)v);
		ASSERT_EQ(menu_selectedPresetNr[SAVE_TYPE_KIT], 5);
		ASSERT_EQ(activeDiffers(0x3C), 0);
		ASSERT_EQ(preset_kitDirty, 0);	/* no autosave of tmp.snd */
	}
}

TEST(presets_bank_select_loads_a_kit)
{
	for (int v = 0; v < NUM_KIT_SLOTS; v++) {
		reset();
		preset_kitDirty = 0;
		bankSelect((uint8_t)v);
		ASSERT_EQ(menu_selectedPresetNr[SAVE_TYPE_KIT], v);
		ASSERT_EQ(activeParameters[0], v);
		ASSERT_EQ(preset_kitDirty, 1);
	}
}

/** a control change as the front panel parser hands it on */
static void controlChange(uint8_t number, uint8_t value)
{
	frontParser_midiMsg.status = MIDI_CC;
	frontParser_midiMsg.data1 = number;
	frontParser_midiMsg.data2 = value;
	frontPanel_ccHandler();
}

/** an NRPN as a controller sends it: CC 99, CC 98, CC 6 */
static void nrpn(uint16_t number, uint8_t value)
{
	controlChange(NRPN_COARSE, (uint8_t)(number >> 7));
	controlChange(NRPN_FINE, (uint8_t)(number & 0x7f));
	controlChange(NRPN_DATA_ENTRY_COARSE, value);
}

#define NRPN_ACTIVE_STEP (PAR_ACTIVE_STEP - 128)	/* the NRPN number is the parameter - 128 */
_Static_assert(NRPN_ACTIVE_STEP == 121, "NRPN 121 is PAR_ACTIVE_STEP");

TEST(presets_nrpn_121_sets_the_active_step)
{
	for (int v = 0; v < NUM_STEPS; v++) {
		reset();
		activeParameters[PAR_ACTIVE_STEP] = 7;
		nrpn(NRPN_ACTIVE_STEP, (uint8_t)v);
		ASSERT_EQ(activeParameters[PAR_ACTIVE_STEP], v);
	}
}

TEST(presets_nrpn_121_past_the_64_steps_is_ignored)
{
	/* the step page writes the step's note (MidiParser.c) and its automation
	 * (menu.c, CortexParser.c) at the active step */
	for (int v = NUM_STEPS; v < 128; v++) {
		reset();
		activeParameters[PAR_ACTIVE_STEP] = 7;
		nrpn(NRPN_ACTIVE_STEP, (uint8_t)v);
		ASSERT_EQ(activeParameters[PAR_ACTIVE_STEP], 7);
	}
}

//------------------------------------------------------------------------------
// The morph flag (stage 1b F10): preset_morphBusy() is 1 while
// preset_morph_tick() still has sound parameters to send
//------------------------------------------------------------------------------
/** preset_morph_tick() until no morph runs, at most limit times; the ticks made */
static int morphUntilIdle(int limit)
{
	int ticks = 0;
	while (preset_morphBusy() && ticks < limit) {
		preset_morph_tick();
		ticks++;
	}
	return ticks;
}

TEST(presets_morph_busy_while_a_morph_runs)
{
	reset();
	morphUntilIdle(END_OF_SOUND_PARAMETERS);	/* a morph that an earlier test started */
	ASSERT_EQ(preset_morphBusy(), 0);
	preset_morph(64);
	ASSERT_EQ(preset_morphBusy(), 1);
	ASSERT_EQ(morphUntilIdle(1000), END_OF_SOUND_PARAMETERS);	/* one tick per sound parameter */
	ASSERT_EQ(preset_morphBusy(), 0);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.2 (plan V1): the extension block after the PatV03 data
//------------------------------------------------------------------------------
#define PAT_V03_SIZE 3663	/* name 8, tag 6, 7 x (448 + 64), 56, shuffle, 7 lengths, kit */
#define PAT_EXT_SIZE 34		/* "LCHM", version 1, 7 tracks, 7 x 4 */

static const TrackGroove noGroove[NUM_TRACKS];	/* the defaults */

/** pattern p with data in every field of the PatV03 part, and settings on every track */
static void fillPattern(uint8_t p, uint8_t salt)
{
	uint8_t *steps = (uint8_t *)seq_patternSet.seq_subStepPattern[p];
	for (size_t i = 0; i < sizeof seq_patternSet.seq_subStepPattern[p]; i++)
		steps[i] = (uint8_t)(i * 7 + salt);
	uint8_t *timing = (uint8_t *)seq_patternSet.seq_stepTiming[p];
	for (size_t i = 0; i < sizeof seq_patternSet.seq_stepTiming[p]; i++)
		timing[i] = (uint8_t)(i * 3 + salt);
	for (int b = 0; b < NUM_BARS; b++)
		for (int t = 0; t < NUM_TRACKS; t++)
			seq_patternSet.seq_mainSteps[b][p][t] = (uint16_t)(0x1234 * (b + 1) + t + salt);
	seq_patternSet.shuffle[p] = (uint8_t)(77 + salt);
	seq_patternSet.kitNr[p] = 9;
	for (int t = 0; t < NUM_TRACKS; t++)
	{
		seq_patternSet.length[p][t] = (uint8_t)(3 + 9 * t);
		seq_patternSet.groove[p][t] = (TrackGroove){ .tempo = (int8_t)(t - 5), .humTime = (uint8_t)t,
			.humVol = (uint8_t)(3 * t + salt % 2), .delay = (int8_t)(4 * t - 15) };
	}
}

typedef struct {
	Step steps[NUM_TRACKS][NUM_STEPS];
	StepTiming timing[NUM_TRACKS][NUM_STEPS];
	uint16_t mainSteps[NUM_BARS][NUM_TRACKS];
	uint8_t length[NUM_TRACKS], shuffle, kit;
	TrackGroove groove[NUM_TRACKS];
} Slot;

static void snap(uint8_t p, Slot *s)
{
	memcpy(s->steps, seq_patternSet.seq_subStepPattern[p], sizeof s->steps);
	memcpy(s->timing, seq_patternSet.seq_stepTiming[p], sizeof s->timing);
	for (int b = 0; b < NUM_BARS; b++)
		memcpy(s->mainSteps[b], seq_patternSet.seq_mainSteps[b][p], sizeof s->mainSteps[b]);
	memcpy(s->length, seq_patternSet.length[p], sizeof s->length);
	s->shuffle = seq_patternSet.shuffle[p];
	s->kit = seq_patternSet.kitNr[p];
	memcpy(s->groove, seq_patternSet.groove[p], sizeof s->groove);
}

/** the card entry of the one file whose path starts with prefix (8.3 names: any case) */
static CardEntry *fileStarting(const char *prefix)
{
	CardEntry *found = NULL;
	const size_t n = strlen(prefix);
	for (int i = 0; i < CARD_ENTRIES; i++)
	{
		if (!card[i].used || card[i].isDir || strlen(card[i].path) < n)
			continue;
		size_t k = 0;
		while (k < n && lowerChar((unsigned char)card[i].path[k]) == lowerChar((unsigned char)prefix[k]))
			k++;
		if (k == n)
		{
			if (found)
				return NULL;
			found = &card[i];
		}
	}
	return found;
}

/** a fresh card with /proj00 as the current folder and pattern 5 saved from
 * fillPattern(5, salt); its file, or NULL (each test asserts it) */
static CardEntry *savedPattern5(uint8_t salt)
{
	reset();
	if (f_mkdir("/proj00") != FR_OK || f_chdir("/proj00") != FR_OK)
		return NULL;
	fillPattern(5, salt);
	if (preset_savePatternSD(5) != FR_OK)
		return NULL;
	return fileStarting("/proj00/05-");
}

TEST(presets_pattern_ext_block_follows_the_v03_part)
{
	CardEntry *e = savedPattern5(0);
	ASSERT_TRUE(e != NULL);
	ASSERT_EQ(e->len, PAT_V03_SIZE + PAT_EXT_SIZE);
	static const uint8_t head[6] = { 'L', 'C', 'H', 'M', 1, 7 };
	ASSERT_EQ(memcmp(e->data + PAT_V03_SIZE, head, sizeof head), 0);
	ASSERT_EQ(memcmp(e->data + PAT_V03_SIZE + 6, seq_patternSet.groove[5], 28), 0);
	/* the PatV03 part is the same whatever the settings are */
	static uint8_t first[PAT_V03_SIZE];
	memcpy(first, e->data, PAT_V03_SIZE);
	memset(seq_patternSet.groove[5], 0, sizeof seq_patternSet.groove[5]);
	ASSERT_EQ(preset_savePatternSD(5), FR_OK);
	e = fileStarting("/proj00/05-");
	ASSERT_TRUE(e != NULL);
	ASSERT_EQ(memcmp(e->data, first, PAT_V03_SIZE), 0);
	ASSERT_EQ(memcmp(e->data + PAT_V03_SIZE + 6, noGroove, sizeof noGroove), 0);
}

TEST(presets_pattern_ext_block_is_invisible_to_the_v03_reading)
{
	CardEntry *e = savedPattern5(1);
	ASSERT_TRUE(e != NULL);
	static uint8_t file[PAT_V03_SIZE + PAT_EXT_SIZE];
	memcpy(file, e->data, sizeof file);
	static Slot with, without, saved;
	snap(5, &saved);
	memset(&seq_patternSet, 0, sizeof seq_patternSet);
	ASSERT_EQ(preset_loadPatternSD(5), FR_OK);
	snap(5, &with);
	/* the same file as fw 1.7 writes it, and stale settings in the slot */
	cardPut(e->path, file, PAT_V03_SIZE);
	memset(&seq_patternSet, 0x5A, sizeof seq_patternSet);
	ASSERT_EQ(preset_loadPatternSD(5), FR_OK);
	snap(5, &without);
	/* everything fw 1.7 reads is the same; the settings are the file's or the defaults */
	ASSERT_EQ(memcmp(&with, &without, offsetof(Slot, groove)), 0);
	ASSERT_EQ(memcmp(&with, &saved, sizeof with), 0);
	ASSERT_EQ(memcmp(without.groove, noGroove, sizeof noGroove), 0);
}

TEST(presets_pattern_ext_block_unknown_or_out_of_range)
{
	CardEntry *e = savedPattern5(0);
	ASSERT_TRUE(e != NULL);
	static uint8_t file[PAT_V03_SIZE + PAT_EXT_SIZE], f[PAT_V03_SIZE + PAT_EXT_SIZE];
	memcpy(file, e->data, sizeof file);
	/* another version, another magic, another track count, a short block: the defaults */
	static const struct { int at; uint8_t value; UINT len; } bad[4] = {
		{ PAT_V03_SIZE + 4, 2, sizeof file }, { PAT_V03_SIZE, 'X', sizeof file },
		{ PAT_V03_SIZE + 5, 6, sizeof file }, { -1, 0, PAT_V03_SIZE + 20 },
	};
	for (unsigned k = 0; k < 4; k++)
	{
		memcpy(f, file, sizeof f);
		if (bad[k].at >= 0)
			f[bad[k].at] = bad[k].value;
		cardPut(e->path, f, bad[k].len);
		fillPattern(5, 0);		/* stale settings */
		ASSERT_EQ(preset_loadPatternSD(5), FR_OK);
		ASSERT_EQ(memcmp(seq_patternSet.groove[5], noGroove, sizeof noGroove), 0);
	}
	/* a field out of range takes its default, the others stay */
	memcpy(f, file, sizeof f);
	uint8_t *g = f + PAT_V03_SIZE + 6;
	g[0] = 6;			/* track 0: tempo index 11 */
	g[4 + 1] = 9;			/* track 1: humanize time 9 ms */
	g[8 + 2] = 26;			/* track 2: humanize volume 26 % */
	g[12 + 3] = 31;			/* track 3: delay +31 ms */
	g[16 + 3] = (uint8_t)-16;	/* track 4: delay -16 ms */
	cardPut(e->path, f, sizeof f);
	ASSERT_EQ(preset_loadPatternSD(5), FR_OK);
	TrackGroove want[NUM_TRACKS];
	fillPattern(6, 0);
	memcpy(want, seq_patternSet.groove[6], sizeof want);
	want[0].tempo = 0;
	want[1].humTime = 0;
	want[2].humVol = 0;
	want[3].delay = 0;
	want[4].delay = 0;
	ASSERT_EQ(memcmp(seq_patternSet.groove[5], want, sizeof want), 0);
}

TEST(presets_tmp_pattern_keeps_the_settings)
{
	reset();
	ASSERT_EQ(f_chdir("/tmp"), FR_OK);
	menu_playedPattern = 3;
	fillPattern(3, 1);
	TrackGroove saved[NUM_TRACKS];
	memcpy(saved, seq_patternSet.groove[3], sizeof saved);
	ASSERT_EQ(preset_saveTmpPat(), FR_OK);
	const int i = find("/tmp/tmp.pat");
	ASSERT_TRUE(i >= 0);
	ASSERT_EQ(card[i].len, 1 + PAT_V03_SIZE - 14 + PAT_EXT_SIZE);
	memset(seq_patternSet.groove[3], 0x11, sizeof saved);
	ASSERT_EQ(preset_LoadTmpPat(), FR_OK);
	ASSERT_EQ(memcmp(seq_patternSet.groove[3], saved, sizeof saved), 0);
	/* tmp.pat of fw 1.7, without the block: the defaults */
	static uint8_t old[1 + PAT_V03_SIZE - 14];
	memcpy(old, card[i].data, sizeof old);
	cardPut("/tmp/tmp.pat", old, sizeof old);
	memset(seq_patternSet.groove[3], 0x11, sizeof saved);
	ASSERT_EQ(preset_LoadTmpPat(), FR_OK);
	ASSERT_EQ(memcmp(seq_patternSet.groove[3], noGroove, sizeof noGroove), 0);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.3 (plan P7): the mute scenes after the project name in prj.nfo
//------------------------------------------------------------------------------
static const uint8_t nfo22[22] = { 'A', 'c', 'i', 'd', ' ', '3', '0', '3', 'L', 'C', 'H', 'M', 1, 8,
				   0x80, 0, 0x85, 0, 0, 0, 0, 0x05 };

static int clearScenesCalls;		/* seq_clearMuteScenes() */
static int sceneQuant = -1;		/* seq_setMuteSceneQuant() */

static void readNfo(const uint8_t *file, UINT len)
{
	char name[8];
	UINT br;
	cardReset();
	cardPut("/prj.nfo", file, len);
	memset(seq_muteScenes, 0x55, MUTE_SCENES);
	ASSERT_EQ(f_open((FIL*)&SDFile, "prj.nfo", FA_OPEN_EXISTING | FA_READ), FR_OK);
	ASSERT_EQ(f_read((FIL*)&SDFile, name, 8, &br), FR_OK);
	preset_readSceneBlock();
	f_close((FIL*)&SDFile);
}

TEST(presets_scene_block_follows_the_project_name)
{
	static const uint8_t scenes[8] = { 0x80, 0, 0x85, 0, 0, 0, 0, 0xFF };
	UINT bw;
	cardReset();
	memcpy(seq_muteScenes, scenes, MUTE_SCENES);
	ASSERT_EQ(f_open((FIL*)&SDFile, "prj.nfo", FA_CREATE_ALWAYS | FA_WRITE), FR_OK);
	ASSERT_EQ(f_write((FIL*)&SDFile, "Acid 303", 8, &bw), FR_OK);
	preset_writeSceneBlock();
	f_close((FIL*)&SDFile);
	const int i = findAbsolute("/prj.nfo");
	ASSERT_TRUE(i >= 0);
	ASSERT_EQ(card[i].len, 22u);
	ASSERT_EQ(memcmp(card[i].data, "Acid 303", 8), 0);
	ASSERT_EQ(memcmp(card[i].data + 8, "LCHM\x01\x08", 6), 0);
	ASSERT_EQ(memcmp(card[i].data + 14, scenes, 8), 0);
}

TEST(presets_scene_block_read_back_and_defaults)
{
	static const uint8_t want[8] = { 0x80, 0, 0x85, 0, 0, 0, 0, 0 };	/* 0x05: not stored */
	readNfo(nfo22, sizeof nfo22);
	ASSERT_EQ(memcmp(seq_muteScenes, want, MUTE_SCENES), 0);
	/* fw 1.7's 8-byte file, a short block, version 2, a count of 7: no scenes */
	static const UINT lens[4] = { 8, 17, 22, 22 };
	for (int k = 0; k < 4; k++)
	{
		uint8_t f[22];
		memcpy(f, nfo22, sizeof f);
		if (k == 2) f[12] = 2;
		if (k == 3) f[13] = 7;
		readNfo(f, lens[k]);
		for (int s = 0; s < MUTE_SCENES; s++)
			ASSERT_EQ(seq_muteScenes[s], 0);
	}
}

// the P5-P8 review: a project load clears the scenes through the sequencer, which drops a
// waiting recall; a new project also sets scn to bar in the sequencer, as its menu shows
TEST(presets_scene_block_read_clears_through_the_sequencer)
{
	const int before = clearScenesCalls;
	readNfo(nfo22, sizeof nfo22);
	ASSERT_EQ(clearScenesCalls, before + 1);
}

TEST(presets_new_project_scene_quant_is_bar)
{
	cardReset();
	sceneQuant = 1;
	ASSERT_EQ(preset_createProject(3), FR_OK);
	ASSERT_EQ(sceneQuant, 0);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.4 (plan A3, D33-D35): the kit extension block and its guard
//------------------------------------------------------------------------------
#define KIT_FW17_SIZE (8 + KIT_SIZE)		/* the name, then the 247 parameters */
#define KIT_BLOCK_COUNT (END_OF_KIT_BLOCK - END_OF_GLOBALS)
#define KIT_V04_SIZE (KIT_FW17_SIZE + 6 + KIT_BLOCK_COUNT)

extern uint8_t lchmKit[NUM_KIT_SLOTS][KIT_BLOCK_COUNT];	/* presetManager.c */

static const uint8_t kitBlockDefaults[KIT_BLOCK_COUNT] = { 100, 64, 10, 40, 0, 100, 0 };

/** a fresh card with /proj00 as the current folder */
static void kitCard(void)
{
	reset();
	ASSERT_EQ(f_mkdir("/proj00"), FR_OK);
	ASSERT_EQ(f_chdir("/proj00"), FR_OK);
}

/** /proj00/05-KIT.SND as fw 1.7 writes it (255 bytes: 0x11, and 36, 40, 45, 50 in the four
 * fw 1.7 PAR_UNUSED slots, 0 in the old reverb type), then the extra bytes */
static void putKit5(const uint8_t *extra, UINT extraLen)
{
	uint8_t file[KIT_FW17_SIZE + 32];
	memcpy(file, "KIT     ", 8);
	memset(file + 8, 0x11, KIT_SIZE);
	file[8 + PAR_DJ_FILTER] = 36;
	file[8 + PAR_DJ_RESO] = 40;
	file[8 + PAR_REV_LEVEL] = 45;
	file[8 + PAR_REV_FREEZE] = 50;
	file[8 + PAR_REV_DECAY] = 0;
	ASSERT_TRUE(extraLen <= 32);
	memcpy(file + KIT_FW17_SIZE, extra, extraLen);
	cardPut("/proj00/05-KIT.SND", file, KIT_FW17_SIZE + extraLen);
}

/** loads kit 5 from the card into its slot and then into the edit buffer; the slot and the
 * block start stale, and the live DJ filter and freeze are 20 and 1 */
static void loadKit5(void)
{
	memset(mainParameters[5], 0x5A, KIT_SIZE);
	memset(lchmKit[5], 0x5A, KIT_BLOCK_COUNT);
	ASSERT_EQ(preset_loadDrumsetSD(5, 0), FR_OK);
	activeParameters[PAR_DJ_FILTER] = 20;
	activeParameters[PAR_REV_FREEZE] = 1;
	preset_loadDrumsetRam(5, 0, 0);
}

/** kit 5 took the D35 defaults: the five slots and the block */
static void assertKit5Defaults(void)
{
	ASSERT_EQ(mainParameters[5][PAR_DJ_FILTER], 64);
	ASSERT_EQ(mainParameters[5][PAR_DJ_RESO], 0);
	ASSERT_EQ(mainParameters[5][PAR_REV_LEVEL], 0);
	ASSERT_EQ(mainParameters[5][PAR_REV_FREEZE], 0);
	ASSERT_EQ(mainParameters[5][PAR_REV_DECAY], 64);
	ASSERT_EQ(mainParameters[5][PAR_FX_P1], 0x11);	/* the rest is the file's */
	ASSERT_EQ(memcmp(lchmKit[5], kitBlockDefaults, KIT_BLOCK_COUNT), 0);
	ASSERT_EQ(activeParameters[PAR_DJ_RESO], 0);
	ASSERT_EQ(activeParameters[PAR_REV_LEVEL], 0);
	ASSERT_EQ(activeParameters[PAR_REV_DECAY], 64);
	ASSERT_EQ(memcmp(&activeParameters[END_OF_GLOBALS], kitBlockDefaults, KIT_BLOCK_COUNT), 0);
	ASSERT_EQ(activeParameters[PAR_DJ_FILTER], 20);	/* live */
	ASSERT_EQ(activeParameters[PAR_REV_FREEZE], 1);
}

TEST(presets_kit_block_round_trip)
{
	kitCard();
	static const uint8_t block[KIT_BLOCK_COUNT] = { 11, 22, 33, 44, 3, 55, 7 };
	memcpy(&activeParameters[PAR_REV_SIZE], block, KIT_BLOCK_COUNT);
	activeParameters[PAR_DJ_RESO] = 90;
	activeParameters[PAR_REV_LEVEL] = 80;
	activeParameters[PAR_REV_DECAY] = 70;
	activeParameters[PAR_DJ_FILTER] = 20;
	activeParameters[PAR_REV_FREEZE] = 1;
	memcpy(savefileNames[5][KIT_NAMES], "KIT     ", 8);
	preset_saveDrumsetRam(5, 0);
	ASSERT_EQ(preset_saveDrumsetSD(5, 0), FR_OK);
	CardEntry *e = fileStarting("/proj00/05-");
	ASSERT_TRUE(e != NULL);
	ASSERT_EQ(e->len, KIT_V04_SIZE);
	ASSERT_EQ(KIT_V04_SIZE, 268);
	static const uint8_t tail[13] = { 'L', 'C', 'H', 'M', 1, 7, 11, 22, 33, 44, 3, 55, 7 };
	ASSERT_EQ(memcmp(e->data + 255, tail, sizeof tail), 0);
	ASSERT_EQ(e->data[8 + 224], 64);	/* the DJ filter is saved flat */
	ASSERT_EQ(e->data[8 + 227], 0);		/* and freeze off */
	ASSERT_EQ(e->data[8 + PAR_DJ_RESO], 90);
	/* load it back over other values */
	memset(&activeParameters[PAR_REV_SIZE], 0, KIT_BLOCK_COUNT);
	activeParameters[PAR_DJ_RESO] = activeParameters[PAR_REV_LEVEL] = activeParameters[PAR_REV_DECAY] = 0;
	memset(mainParameters[5], 0x5A, KIT_SIZE);
	memset(lchmKit[5], 0x5A, KIT_BLOCK_COUNT);
	ASSERT_EQ(preset_loadDrumsetSD(5, 0), FR_OK);
	preset_loadDrumsetRam(5, 0, 0);
	ASSERT_EQ(memcmp(&activeParameters[PAR_REV_SIZE], block, KIT_BLOCK_COUNT), 0);
	ASSERT_EQ(activeParameters[PAR_DJ_RESO], 90);
	ASSERT_EQ(activeParameters[PAR_REV_LEVEL], 80);
	ASSERT_EQ(activeParameters[PAR_REV_DECAY], 70);
	ASSERT_EQ(activeParameters[PAR_DJ_FILTER], 20);	/* the live values stay */
	ASSERT_EQ(activeParameters[PAR_REV_FREEZE], 1);
	ASSERT_EQ(kitsGuardChanged(), 0);
}

TEST(presets_kit_fw17_length_takes_defaults)
{
	kitCard();
	putKit5(NULL, 0);
	loadKit5();
	assertKit5Defaults();
}

TEST(presets_kit_block_other_version)
{
	kitCard();
	static const uint8_t block[13] = { 'L', 'C', 'H', 'M', 2, 7, 11, 22, 33, 44, 3, 55, 7 };
	putKit5(block, sizeof block);
	loadKit5();
	assertKit5Defaults();
	/* a short block too */
	putKit5(block, 9);
	loadKit5();
	assertKit5Defaults();
}

TEST(presets_kit_block_longer_count)
{
	kitCard();
	static const uint8_t block[15] = { 'L', 'C', 'H', 'M', 1, 9, 11, 22, 33, 44, 3, 55, 7, 99, 98 };
	putKit5(block, sizeof block);
	loadKit5();
	static const uint8_t first7[KIT_BLOCK_COUNT] = { 11, 22, 33, 44, 3, 55, 7 };
	ASSERT_EQ(memcmp(lchmKit[5], first7, KIT_BLOCK_COUNT), 0);
	ASSERT_EQ(memcmp(&activeParameters[END_OF_GLOBALS], first7, KIT_BLOCK_COUNT), 0);
	/* with a valid block the five slots are the file's */
	ASSERT_EQ(mainParameters[5][PAR_DJ_RESO], 40);
	ASSERT_EQ(mainParameters[5][PAR_REV_LEVEL], 45);
	ASSERT_EQ(mainParameters[5][PAR_REV_DECAY], 0);
	ASSERT_EQ(activeParameters[PAR_DJ_FILTER], 20);	/* live */
	ASSERT_EQ(activeParameters[PAR_REV_FREEZE], 1);
}

TEST(presets_kit_block_values_out_of_range)
{
	/* each value past its maximum (127, 127, 127, 127, 6, 127, 9) takes its default */
	kitCard();
	static const uint8_t block[13] = { 'L', 'C', 'H', 'M', 1, 7, 128, 127, 255, 0, 7, 6, 10 };
	putKit5(block, sizeof block);
	loadKit5();
	static const uint8_t checked[KIT_BLOCK_COUNT] = { 100, 127, 10, 0, 0, 6, 0 };
	ASSERT_EQ(memcmp(lchmKit[5], checked, KIT_BLOCK_COUNT), 0);
	/* the largest values are kept */
	static const uint8_t top[13] = { 'L', 'C', 'H', 'M', 1, 7, 127, 127, 127, 127, 6, 127, 9 };
	putKit5(top, sizeof top);
	loadKit5();
	ASSERT_EQ(memcmp(lchmKit[5], top + 6, KIT_BLOCK_COUNT), 0);
}

TEST(presets_glo_cfg_stays_30_bytes)
{
	reset();
	ASSERT_EQ(f_chdir("/"), FR_OK);
	for (int i = PAR_BEGINNING_OF_GLOBALS; i < NUM_PARAMS; i++)
		activeParameters[i] = (uint8_t)i;
	ASSERT_EQ(preset_saveGlobals(), FR_OK);
	const int g = find("/glo.cfg");
	ASSERT_TRUE(g >= 0);
	ASSERT_EQ(card[g].len, 30);
	ASSERT_EQ(card[g].data[29], (uint8_t)PAR_PATTERN_MODE);
	/* a 30-byte glo.cfg loads, and leaves the kit block alone */
	for (int i = PAR_BEGINNING_OF_GLOBALS; i < NUM_PARAMS; i++)
		activeParameters[i] = 0;
	ASSERT_EQ(preset_loadGlobals(), 1);
	ASSERT_EQ(activeParameters[PAR_BPM], (uint8_t)PAR_BPM);
	ASSERT_EQ(activeParameters[PAR_PATTERN_MODE], (uint8_t)PAR_PATTERN_MODE);
	ASSERT_EQ(activeParameters[PAR_REV_SIZE], 0);
}

TEST_MAIN()

//------------------------------------------------------------------------------
// Firmware state that presetManager.c and frontPanelParser.c read and write
//------------------------------------------------------------------------------
FIL SDFile;
TIM_HandleTypeDef htim2;
volatile uint32_t systick_ticks;
uint8_t lcd_process_display_interrupt;

uint8_t activeParameters[NUM_PARAMS];
uint8_t *morphParameters;
uint8_t parameterOrder[END_OF_SOUND_PARAMETERS];
uint8_t paramToModTarget[END_OF_SOUND_PARAMETERS];
const uint8_t initKit[INIT_KIT_SIZE];
const ModTarg modTargets[256];

PatternSet seq_patternSet;
uint8_t seq_songData[NUM_KIT_SLOTS][MAX_SONG_LENGTH];
uint8_t *seq_activeSong = seq_songData[0];
uint8_t seq_activePattern;
uint8_t seq_pendingPattern;

uint8_t menu_selectedPresetNr[NUM_PRESET_LOCATIONS];
uint8_t menu_loadedPresetNr[NUM_CURRENT];
uint8_t menu_playedPattern;
uint8_t menu_shownPattern;
uint8_t menu_activePage;

//------------------------------------------------------------------------------
// Stand-ins for the rest of the firmware
//------------------------------------------------------------------------------
uint32_t HAL_GetTick(void) { return systick_ticks; }
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim) { (void)htim; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim) { (void)htim; return HAL_OK; }

uint8_t getNumModTargets(void) { return 64; }
void lcd_clear(void) {}
void lcd_home(void) {}
void lcd_setcursor(uint8_t spalte, uint8_t zeile) { (void)spalte; (void)zeile; }
void lcd_string(const char *data) { (void)data; }
uint8_t sendDisplayBuffer(void) { return 0; }
void menu_repaint(void) {}
void menu_repaintAll(void) {}
void menu_sendAllGlobals(void) {}
void menu_setNumSamples(uint8_t num) { (void)num; }
void menu_setShownPattern(uint8_t patternNr) { (void)patternNr; }
void midiParser_ccHandler(MidiMsg msg, uint8_t updateOriginalValue, uint8_t isFloatModulator, float v)
{
	(void)msg; (void)updateOriginalValue; (void)isFloatModulator; (void)v;
}
void seq_clearPattern(uint8_t pattern) { (void)pattern; }
void seq_copyPattern(uint8_t src, uint8_t dst) { (void)src; (void)dst; }
void seq_copyPatternToBackupBuffer(uint8_t patternNr) { (void)patternNr; }
void seq_setNextPattern(const uint8_t patNr) { (void)patNr; }
uint8_t seq_muteScenes[MUTE_SCENES];	/* LICHOMAT v0.3 (plan P7) */
void seq_clearMuteScenes(void) { clearScenesCalls++; memset(seq_muteScenes, 0, MUTE_SCENES); }
void seq_setMuteSceneQuant(uint8_t step) { sceneQuant = step; }
void soundengine_process(void) {}
void uart_checkAndParse(void) {}
uint8_t buttonHandler_getMode(void) { return 0; }
void buttonHandler_muteVoice(uint8_t voice, uint8_t isMuted) { (void)voice; (void)isMuted; }
void buttonHandler_setRunStopState(uint8_t running) { (void)running; }
void frontParser_parseUartData(unsigned char data) { (void)data; }
void led_clearAllBlinkLeds(void) {}
void led_clearSequencerLeds(void) {}
void led_initPatternSelectLeds(void) {}
void led_pulseLed(uint8_t ledNr) { (void)ledNr; }
void led_setValue(uint8_t val, uint8_t ledNr) { (void)val; (void)ledNr; }
void led_updateSeqLeds(void) {}

/* as in menu.c */
void numtostrpu_2Bytes(char *buf, uint8_t num, char pad)
{
	(void)pad;
	if (num > 99) {
		buf[0] = '>';
		buf[1] = '>';
		return;
	}
	buf[0] = (char)('0' + num / 10);
	buf[1] = (char)('0' + num % 10);
}

void numtostrpu(char *buf, uint8_t num, char pad)
{
	buf[0] = num > 99 ? (char)('0' + num / 100) : pad;
	buf[1] = (num > 9) ? (char)('0' + (num / 10) % 10) : (buf[0] == pad ? pad : '0');
	buf[2] = (char)('0' + num % 10);
}
