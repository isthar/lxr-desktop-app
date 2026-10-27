/*
 * test_presets.c
 *
 * Host test of the preset manager's memory accesses (task E4.7): the project
 * re-save (empty_directory() in Core/Src/Preset/presetManager.c), and the
 * kit-slot and project numbers that come off the card or in by MIDI. fw 1.7
 * wrote and read past the 13-byte currentFolder, the 64 kits of mainParameters
 * and the 64 slots of savefileNames for them.
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

static void absolutePath(char *out, const char *path)
{
	if (path[0] == '/')
		snprintf(out, CARD_PATH, "%s", path);
	else if (strcmp(cardCwd, "/") == 0)
		snprintf(out, CARD_PATH, "/%s", path);
	else
		snprintf(out, CARD_PATH, "%s/%s", cardCwd, path);
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
	 * six LFO voice numbers of the kit, 17, back to 1) */
	reset();
	preset_loadDrumsetRam(17, 0, 0);
	ASSERT_EQ(activeDiffers(17), 6);
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
