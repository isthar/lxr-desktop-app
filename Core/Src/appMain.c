/*
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

/* The application's boot and one pass of its main loop, moved out of main.c,
 * which keeps the CubeMX initialisation and calls them (stage 1b F4). */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"
#include "appMain.h"

//used for bootloader update.
//If 1, the main firmware will load a bootloader.img file to the bootloader section from SD card
#define UPDATE_BOOTLOADER 0

#if UPDATE_BOOTLOADER
#warning "UPDATE_BOOTLOADER is enabled: this firmware removes the bootloader's write protection and overwrites the bootloader from the SD card at boot. Never publish it as a normal firmware update; an interrupted or wrong bootloader update can only be repaired with a debug probe."
#include "flash_if.h"

typedef struct ImageFileHeaderStruct
{
	char fileHeader[8];		// ("LXRV2IMG") 8 bytes
	uint32_t datasize;		// size of the data blob, 4 bytes
	char checksum;			// 1 byte checksum
	char unused[3];			// 3 bytes padding to make header 16 byte long

} ImageFileHeader;

#define HEADER_SIZE sizeof(ImageFileHeader)
#endif

/* Private includes ----------------------------------------------------------*/
#include "lcd.h"
#include "menu/menu.h"
#include "IO/DIO.h"
#include "menu/screensaver.h"
#include "menu/copyClearTools.h"
#include "ledHandler.h"
#include "buttonHandler.h"
#include "encoder.h"
#include "IO/adcPots.h"
#include "Preset/PresetManager.h"
#include "Soundengine/Soundengine.h"
#include "Soundengine/DSPAudio/random.h"
#include "Soundengine/USB/usb_manager.h"
#include "Soundengine/MIDI/Uart.h"

// The CMake build passes FIRMWARE_NAME (LICHOMAT_NAME) and FIRMWARE_VERSION (project(VERSION));
// these are the fallbacks for other builds.
#ifndef FIRMWARE_NAME
#define FIRMWARE_NAME "LICHOMAT"
#endif
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.1"
#endif

/* CubeMX handles that main.c defines and main.h does not declare */
extern ADC_HandleTypeDef hadc1;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim14;

/* locals of main() before F4: the main loop reads them */
static FRESULT res;
static uint8_t lastSdState;

void sd_formatRoutine()
{

	lcd_process_display_interrupt = 0;
	//menu to ask user to format card
	lcd_clear();
	lcd_home();
	lcd_string("Format card? All");
	//goto 2nd line
	lcd_setcursor(0,2);
	lcd_string("data erased?");

	while(encode_readButton()==0);
	while(encode_readButton()==1);

	lcd_clear();
	lcd_home();
	lcd_string("Really Format");
	//goto 2nd line
	lcd_setcursor(0,2);
	lcd_string("card?");

	while(encode_readButton()==0);

	//make fs
	FRESULT res;
	FATFS fs;           /* Filesystem object */
	BYTE work[512]; /* Work area (larger is better for processing time) */


	lcd_clear();
	lcd_home();
	lcd_string("Formatting card");
	//goto 2nd line
	lcd_setcursor(0,2);
	lcd_string("...");

	res = f_mkfs("", FM_FAT32, 0, work, 512);

	if(res!=FR_OK)
	{
		//error, can't format card
		lcd_clear();
		lcd_home();
		lcd_string("Error-Please try");
		//goto 2nd line
		lcd_setcursor(0,2);
		lcd_string("another card!");
		while(1);
	}
	else
	{
		/* Gives a work area to the default drive */
		f_mount(&fs, "", 0);

		//create glo.cfg file
		preset_createGlobals();

		//load init sound
		preset_init(0);
		//save init sound to card

		sprintf(savefileNames[0][PROJECT_NAMES], "Init");
		//preset_saveDrumsetSD(0);
		preset_saveProject(0);

		//success
		lcd_clear();
		lcd_home();
		lcd_string("Success!");
		//goto 2nd line
		lcd_setcursor(0,2);
		lcd_string("Please reboot.");

		while(1);
	}
}

volatile static int8_t checkMainLoc = 0;

/** the boot: main.c's code from dio_init() to the EXTI enables */
void appMain_boot(void)
{
  dio_init(&hspi1);





  lcd_init();
  lcd_clear();
  lcd_home();
 // usb_init();




 
  menu_init();
  //HAL_ADC_Start(&hadc1);




  //we have to read out the pots once to have a defined state before loading the preset data
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, ADC_BUF_LEN);
  adc_checkPots();
  adc_checkPots();

  MX_FATFS_ReInit();

#if UPDATE_BOOTLOADER
  //print boot up message
  lcd_clear();
  lcd_string("Updating");
  //goto 2nd line
  lcd_setcursor(0,2);
  lcd_string("Bootloader...");

#else
	//print boot up message: LICHOMAT is modified firmware, and its start-up screen
	//must say so (NOTICE.md, 7(c))
lcd_clear();
lcd_string(FIRMWARE_NAME);
//goto 2nd line
lcd_setcursor(0,2);
lcd_string("v" FIRMWARE_VERSION " unofficial");
#endif

lcd_showSplash();


uint8_t sd_card_ok = 0;
//---- mount sd card ----  //todo move to preset manager or other matching .c file
//FatFs[0] = 0;
res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);
if( res != FR_OK)
{

	lcd_home();

	//uint8_t error = 1;
	switch(res)
	{
		// There is no valid FAT volume on the drive or wrong lower layer implementation.
		case FR_NO_FILESYSTEM:
			lcd_clear();
			lcd_string("SD error:");
			//goto 2nd line
			lcd_setcursor(0,2);
			lcd_string("No FS found!");
			HAL_Delay(1000);
			sd_formatRoutine();
			break;

		// The lower layer, disk_read, disk_write or disk_ioctl function, reported that an unrecoverable hard error occured.
		case FR_DISK_ERR:
			lcd_clear();
			lcd_string("SD error:");
			//goto 2nd line
			lcd_setcursor(0,2);
			lcd_string("Disk error!");
			HAL_Delay(1000);
			break;

		/*The lower layer, disk_initialize function, reported that the storage device could not be got ready to work. One of the following possibilities is suspected.

		No medium in the drive.
		Wrong lower layer implementation.
		Wrong hardware configuration.
		The storage device has broken.
		*/
		case FR_NOT_READY:
			lcd_clear();
			lcd_string("SD error:");
			//goto 2nd line
			lcd_setcursor(0,2);
			lcd_string("No Card!");
			HAL_Delay(1000);
			break;



		default:
			lcd_clear();
			lcd_string("SD error:");
			//goto 2nd line
			lcd_setcursor(0,2);
			lcd_string("unknown!");
			HAL_Delay(1000);
			break;

	}



}
else
{
	sd_card_ok = 1;
}

//we have enough memory now, no more need for those stupid temp patterns!
//check if temp pattern files exist
//if(sd_card_ok) sd_checkTempFiles();

HAL_Delay(1000);
lcd_clear();

adc_checkPots();
adc_checkPots();

//-----------------------

sprintf(currentFolder, "/proj00");
if(sd_card_ok)
{




	FRESULT res;
	FILINFO fno;

	res = f_stat(currentFolder, &fno);
	if(res == FR_NO_FILE)
	{
		res = f_mkdir(currentFolder);
		if(res == FR_OK)
		{
			f_chdir(currentFolder);
		}
	}
	else
	{
		f_chdir(currentFolder);
	}
}

	soundengine_init();

	if(sd_card_ok)
	{
		if(!preset_loadGlobals())
		{
			lcd_clear();
			lcd_home();
			lcd_string(PSTR("glo.cfg error"));
			lcd_setcursor(0,2);
			lcd_string("Using defaults.");
			preset_createGlobals();
			HAL_Delay(1000);

		}
	}

	//load init preset
	if(sd_card_ok)
	{



		f_chdir(currentFolder);

#if 1

#if LOAD_PROJECT_ON_BOOT


#else
		//load all 64 kits from sd card
		lcd_clear();
		lcd_home();
		lcd_string(PSTR("Loading kits"));
		lcd_setcursor(0,2);
		lcd_string("from SD card");

		for(uint8_t i=0;i<NUM_KIT_SLOTS;i++)
		{
			preset_loadDrumsetSD(i,0);
		}
#endif

		//init morph target to peset 0
		morphParameters = mainParameters[0];

#if LOAD_PROJECT_ON_BOOT

#else
		//init to preset 1
		preset_loadDrumsetRam(0,0, 1);
#endif
#else
		if(!preset_loadDrumsetSD(0,0)) {
			lcd_clear();
			lcd_home();
			lcd_string(PSTR("Kit read error"));
			HAL_Delay(1000);
			preset_sendDrumsetParameters(); // send initial parameters (all 0's)
		}
#endif
	}

  //initialize empty pattern
if(sd_card_ok)
{

#if UPDATE_BOOTLOADER
	typedef  void (*pFunction)(void);
			  pFunction Jump_To_Application;
			  uint32_t JumpAddress;
			 // FIL firmwareFile;		/* place to hold 1 file*/

			  HAL_Delay(100);

			  //--------------------------------------------------------------------
			  //
			  //--------------------------------------------------------------------
			    FLASH_OBProgramInitTypeDef pOBInit;
			    HAL_FLASHEx_OBGetConfig(&pOBInit);

			    if(!(OB_WRP_SECTOR_0 & pOBInit.WRPSector))
			    {

			    	lcd_clear();
					lcd_home();
					lcd_string(PSTR("unlock sec 1..."));
					HAL_Delay(1000);

			  	  //sector 0 write protected -> unlock it
			  	  pOBInit.OptionType = OPTIONBYTE_WRP;

			  	  pOBInit.WRPState = OB_WRPSTATE_DISABLE;
			  	  pOBInit.WRPSector = OB_WRP_SECTOR_0;

			  	  // unlock FLASH in general
			  	  if(HAL_FLASH_Unlock() == HAL_OK) {
			  	     // unlock option bytes in particular
			  	     if(HAL_FLASH_OB_Unlock() == HAL_OK) {
			  	        // erase option bytes before programming

			  	           // program selected option byte
			  	            if(HAL_FLASHEx_OBProgram(&pOBInit) != HAL_OK)
			  	            {
			  	              HAL_FLASH_OB_Lock();
			  	              HAL_FLASH_Lock();

			  	            }
			  	            else
			  	            {
			  					HAL_FLASH_OB_Launch();
			  					HAL_FLASH_OB_Lock();
			  					HAL_FLASH_Lock();
			  	            }
			  	     }
			  	  }
			    }
			    //--------------------------------------------------------------------

				//start bootloader
		    	lcd_clear();
							lcd_home();
				lcd_string("update 2/3...");
				HAL_Delay(500);


				ImageFileHeader imageHeader;
				FRESULT fr;     /* Return value */

				 DIR dj;         /* Directory object */
				FILINFO fno;    /* File information */




				//open bootloader.img
				f_chdir("/");

				//create bootloader start file
			//	fr = f_open((FIL*)&SDFile,"REBOOT.CMD",FA_CREATE_NEW | FA_WRITE);
			//	f_close((FIL*)&SDFile);
				//res = f_open((FIL*)&SDFile,filename,FA_CREATE_ALWAYS | FA_WRITE);
				fr = f_open((FIL*)&SDFile,"BOOTLO~1.IMG",FA_OPEN_EXISTING | FA_READ);
				if(fr!=FR_OK)
				{
					//error
					lcd_clear();
					lcd_home();
					lcd_string("Can't open");
					lcd_setcursor(0,2);
					lcd_string("LXR*.img!");
					while(1);
				}

				//get filesize in bytes
				FSIZE_t filesize = f_size((FIL*)&SDFile);

				lcd_clear();
				lcd_home();
				lcd_string("Checking");
				lcd_setcursor(0,2);
				lcd_string("Firmware...");

				//read file header
				UINT bytesRead;
				f_read((FIL*)&SDFile, (void*)&imageHeader, HEADER_SIZE, &bytesRead);

				//header sanity check
				if(strncmp(imageHeader.fileHeader, "LXRV2IMG", (unsigned int)8) != 0)
				{
					//error -> filetype header tag does not match!
					lcd_clear();
					lcd_home();
					lcd_string("LXR*.img");
					lcd_setcursor(0,2);
					lcd_string("Not Valid!");
					while(1);
				}

				//Filesize check
				filesize = imageHeader.datasize;
				if( (filesize <= 0) || (filesize >= (ADDR_FLASH_SECTOR_1)))
				{
					lcd_clear();
					lcd_home();
					lcd_string("bootloader.img");
					lcd_setcursor(0,2);
					lcd_string("Wrong Size!");
					while(1);
				}

				//checksum calculation
				uint8_t sum = 0;
				for(uint32_t i=0; i<filesize; i++)
				{
					char byte;
					f_read((FIL*)&SDFile, (void*)&byte, 1, &bytesRead);
					sum += byte;
					if(bytesRead==0) break;
				}
				sum += imageHeader.checksum;

				if(sum != 0xff)
				{
					lcd_clear();
					lcd_home();
					lcd_string("Wrong");
					lcd_setcursor(0,2);
					lcd_string("Checksum!");
					while(1);
				}

				//rewind to firmware datablob
				f_lseek((FIL*)&SDFile, (FSIZE_t)HEADER_SIZE );
				//proceed to load image to flash

				//------- flash new bootloader ------------
				FLASH_If_Init();

				uint32_t SectorError;
				FLASH_EraseInitTypeDef eraseInit;
				eraseInit.TypeErase 	= FLASH_TYPEERASE_SECTORS;
				eraseInit.Sector 		=  FLASH_SECTOR_0;
				eraseInit.NbSectors	 	= 1; //sectors 1-7
				eraseInit.VoltageRange 	= FLASH_VOLTAGE_RANGE_3;


				HAL_FLASHEx_Erase(&eraseInit, &SectorError);

				if(SectorError != 0xFFFFFFFF)
				{
					lcd_clear();
					lcd_home();
					lcd_string("Can't erase");
					lcd_setcursor(0,2);
					lcd_string("Flash!");
					while(1);
				}


				lcd_clear();
				lcd_home();
				lcd_string("Loading Firmware");
				lcd_setcursor(0,2);
				lcd_string("0%");

	#define DATA_BUFFER_SIZE 16

			uint32_t bytesWritten=0;
			uint32_t data[DATA_BUFFER_SIZE];
			uint8_t eof_reached = 0;

			while(!eof_reached)
			{
				f_read((FIL*)&SDFile, data, DATA_BUFFER_SIZE*4, &bytesRead);

				if(bytesRead)
				{
					//program flash
					uint32_t addr = APPLICATION_ADDRESS+bytesWritten;

					//check address sanity
					if( (addr < APPLICATION_ADDRESS) || (addr >=(ADDR_FLASH_SECTOR_1)) )
					{
						lcd_clear();
						lcd_home();
						lcd_string("Address");
						lcd_setcursor(0,2);
						lcd_string("Error!");
						while(1);
					}
	#if !USE_NEW_FLASH_IF
					if(FLASH_If_Write((__IO uint32_t*)(&addr), data, bytesRead/4) != 0)
					{
						lcd_clear();
						lcd_home();
						lcd_string("Flash write");
						lcd_setcursor(0,2);
						lcd_string("Error!");
						while(1);
					}
	#else
					HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data);
	#endif


				}
				else
				{
					//file finished
					eof_reached=1;
				}

				//update Display
				bytesWritten += bytesRead;

				if(bytesWritten == filesize)
				{
					//datablob finished
					eof_reached=1;
				}
				//lcd_clear();
				//lcd_home();
				//lcd_string("Loading Firmware");
				char text[17];
				sprintf(text, "%d%%", (uint8_t)(bytesWritten/(float)filesize * 100.f));
				lcd_setcursor(0,2);
				lcd_string(text);
			}

			//delete LXR*.img and rename main.img to LXR_V2_V1.3b_17_02_2022
		 //   DIR dj;         /* Directory object */
		//    FILINFO fno;    /* File information */
		    fr = f_findfirst(&dj, &fno, "", "LXR*.img");  /* Start to search for LXR image */
		    if(fr == FR_OK && fno.fname[0])
		    {
		    	f_unlink(fno.fname);
		    	fr = f_rename("MAIN.IMG", "LXR_V2.IMG");
		    }



			//programming finished - reboot message
			lcd_clear();
			lcd_home();
			lcd_string("Please reboot");
			//goto 2nd line
			lcd_setcursor(0,2);
			lcd_string("+ hold encoder!");
			//NVIC_SystemReset();
			while(1);
	}	//LICHOMAT: closes if(sd_card_ok) in the bootloader build (F4)
#else

#if LOAD_PROJECT_ON_BOOT

#else
	for(uint8_t i=0;i<NUM_KIT_SLOTS;i++)
	{

		preset_loadPatternSD(i);
	}
	seq_copyPatternToBackupBuffer(0);
	led_updateSeqLeds();
#endif
	//load project + song names
	for(uint8_t i=0;i<NUM_KIT_SLOTS;i++)
	{
		preset_loadProjectNames(i);
	}
	//return to project 0
	f_chdir("/proj00");

#if LOAD_PROJECT_ON_BOOT
	uint8_t lastUsedProject;
	preset_loadLastUsedProjectNrFromSD(&lastUsedProject);
	//do not clean the tmp folder here, as we are just booting
	preset_loadProject(lastUsedProject, 0);
	if(res != FR_OK)
	{
		menu_showSdError(res);
	}
	else
	{
		menu_loadedPresetNr[CURRENT_PROJECT] = menu_selectedPresetNr[SAVE_TYPE_PROJECT] = lastUsedProject;
		//menu_resetSaveParameters();
	}
#else
	for(uint8_t i=0;i<NUM_KIT_SLOTS;i++)
	{
		preset_loadSongSD(i);
	}
	//if pattern change mode is auto load corresponding kit
	if(activeParameters[PAR_PATTERN_MODE] == 1)
	{
		preset_loadDrumsetRam(seq_patternSet.kitNr[seq_activePattern], 0, 1);
		menu_selectedPresetNr[SAVE_TYPE_KIT] = seq_patternSet.kitNr[seq_activePattern];
	}
#endif


}
else
{
  copyClear_clearCurrentPattern();
  preset_init(0);
  preset_loadDefaultGlobals();
}





//f_unmount((TCHAR const*)SDPath);



#if USE_FADER_VOLUME
 // HAL_ADC_Start_DMA(&hadc2, (uint32_t*)adc2_buf, ADC2_BUF_LEN);
#endif
  initMidiUart();
 // __HAL_UNLOCK(&huart3);
  //clear "Overrun error" flag if midi is received during boot
  __HAL_UART_CLEAR_OREFLAG(&huart3);


  __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);




  HAL_TIM_Base_Start_IT(&htim14); 	//our own own systick - prio 0 - higher prio comes first
#if CALC_SAMPLES_IN_INTERRUPT
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ

#else
  HAL_TIM_Base_Start_IT(&htim2);	//calc sample interrupt - prio 1
#endif
#endif
 // HAL_TIM_Base_Start_IT(&htim13);


lastSdState = BSP_SD_IsDetected();
buttonHandler_init(); //to make sure all leds are properly lit after boot

//make sure to have the tmp folder active
preset_initTmpFolder();
//----- load tmp stuff --------
//manually saved files
loadTmpFolderFiles();
//autosave kit
preset_LoadTmpKit();
//autosave pattern
preset_LoadTmpPat();
led_updateSeqLeds();

preset_resetAllSaveTimers();
//-----------------------------

lcd_process_display_interrupt = 1;

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
#endif
}

/** one pass of the main loop */
void appMain_loopOnce(void)
{
#if !UPDATE_BOOTLOADER
  checkMainLoc = -14;

	  //check SD card
	  if((BSP_SD_IsDetected() == SD_PRESENT) &&  (lastSdState == SD_NOT_PRESENT))
	  {
			MX_FATFS_ReInit();
			f_mount(0, (TCHAR const*)SDPath, 0); //un-mount
			memset((FATFS*)&SDFatFS,0,sizeof(FATFS)); //clear FS object to force reinit
			res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);
			if( res != FR_OK)
			{
				menu_showSdError( res);
			}
			else
			{
				//load project + song names
				for(uint8_t i=0;i<NUM_KIT_SLOTS;i++)
				{
					preset_loadProjectNames(i);
				}
			}

	  }
	  lastSdState = BSP_SD_IsDetected();

//	  soundengine_process();
	  //TODO clean up this multi call mess!!!!

    checkMainLoc = -13;
	//  menu_tick();
	  adc_checkPots();
	  soundengine_process();
	//  menu_tick();
    checkMainLoc = -12;
	  buttonHandler_tick();
	  adc_checkPots();
	//  menu_tick();
  checkMainLoc = -11;
	  //get the encoder values
	  menu_parseEncoder(encode_read4(),encode_readButton());
	//  menu_tick();
	  led_tickHandler();
	//  menu_tick();
	  adc_checkPots();
	  soundengine_process();
	//  menu_tick();
	  screensaver_check();
	//  menu_tick();
  checkMainLoc = -10;

	  //update the menu according to the encoder values
	  dio_process();
	//  menu_tick();
	  adc_checkPots();
    checkMainLoc = -9;
	//  menu_tick();
	  soundengine_process();
	//  menu_tick();
	  adc_checkFaders();
	//  menu_tick();
    checkMainLoc = -8;

	  //MIDI input (DIN and USB) is parsed in seq_tick() (D8)

	  //check if we have to save something
	  preset_processTimer();

    checkMainLoc = -7;


    /* USER CODE END WHILE */
#endif
}
