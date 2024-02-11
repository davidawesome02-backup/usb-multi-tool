// RECIENT CHANGES:
// was adding help_files, but got distracted modifying and making the basic image better; should be good now



//	Hid and usb sd passthrough with image faking
//
//	Define used header files to allow access to functions used inside the program

#define DBG_TAG "MAIN"
#define async_writes false//true
#define async_fs_writes false//true
#define flash_mode true
#define help_files true

#include "usbh_core.h"
#include "bflb_mtimer.h"
#include "board.h"
#include "fatfs_diskio_register.h"
#include "ff.h"
#include "diskio.h"
#include "usbd_msc.h"
#include "usbd_core.h"
#include "bflb_gpio.h"
#include "bflb_flash.h"
#include "bl808_l1c.h"
#include "raw_data_files.h"

// used for raw uart
#include <bflb_uart.h>


// Import logging functions add add tags for use in debuging
#include "log.h"




#define FLASH_ROM_OFFSET_M0 0x58000000 
void ATTR_TCM_SECTION reboot_chip(uint32_t addr) {
	((void (*)(void))addr)();
	while (1);
}



// Structure of a hid mouse, this will be directly passed over usb after headers are added
struct hid_mouse {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
};

// Create a instance of hid_mouse for use with hid_composite operations protaining to the mouse
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX struct hid_mouse mouse_cfg;


// Structure for returning value out of the parse_config function
typedef struct {
    TCHAR path[24];
    TCHAR name[40];
    FSIZE_t size; 
    bool ro;
    unsigned int err_code; //255 = unk
    unsigned int bs;
    unsigned int blockcount;
	TCHAR hid_file[40];
	TCHAR selected_type[15];
	bool cache_writes;
	bool cd_img;
} PARSED_CONFIG;


struct hid_msc {
    uint32_t blocksize;
    uint32_t blockcount;
    bool ro;
    FIL read_file;
    FIL cache_file;
    bool write_cacheing;
    // DWORD *write_cacheing_table;
	FIL cache_table_file;
	DWORD write_cacheing_length;
    // bool created;
    bool cd_img;

};


#if async_fs_writes

typedef struct {
    unsigned char operation; // 1 == read; 2 == write
	uint32_t length;
	uint64_t sector;
	uint8_t *buffer; // should be a null buffer for read
} fatfs_function_call;

volatile fatfs_function_call buffer_fatfs_function;
#endif

#if flash_mode 

volatile uint32_t flash_total_length = 0x00;
volatile bool flash_done = false;
volatile bool flash_correct_board = true;

typedef struct {
    // 32 byte header
    uint32_t magicStart0;
    uint32_t magicStart1;
    uint32_t flags;
    uint32_t targetAddr;
    uint32_t payloadSize;
    uint32_t blockNo;
    uint32_t numBlocks;
    uint32_t fileSize; // or familyID;
    uint8_t data[476];
    uint32_t magicEnd;
} UF2_Block;


#endif

#if help_files
//^.*? 0x(.*?) [\/\\ ]*(.*?)$
/*
for (i=0;i<255;i++) {
    if (!asd[i]) {asd[i] = "N/A"}
}

out = ""
for (i=0;i<255;i++) {
    if (!asd[i]) asd[i] = ""
    out += escape(asd[i])+"\0"
}

*/

    
#endif

uint32_t endian_swap_uint32_t(uint32_t in)
{
    uint32_t out;
    uint8_t *indata = (uint8_t *)&in;
    uint8_t *outdata = (uint8_t *)&out;
    outdata[0] = indata[3] ;
    outdata[3] = indata[0] ;

    outdata[1] = indata[2] ;
    outdata[2] = indata[1] ;
    return out;
}

// void endian_swap_uint32_t_self_modify(uint32_t *in)
// {
//     uint32_t out;
//     uint8_t *indata = (uint8_t *)in;
//     uint8_t *outdata = (uint8_t *)&out;
//     outdata[0] = indata[3] ;
//     outdata[3] = indata[0] ;

//     outdata[1] = indata[2] ;
//     outdata[2] = indata[1] ;
// 	memcpy(in,&out,4);
// }


// Define function pointers for mass storage read and write to refrence, these will be defined as refrences to fs_read_file or msc_read_block based on current config, this will change durring runtime
int (*sector_write)(uint32_t, uint8_t *, uint32_t);
int (*sector_read)(uint32_t, uint8_t *, uint32_t);


extern void hid_disconnect(void);
extern void hid_init(void);
extern void sendmouse(struct hid_mouse mouse,int type_selected);
extern bool wait_for_usb_connection(void);
extern void send_str(char data[],uint8_t modifiers,int resend_count, int type_selected);
extern uint8_t ascii_to_hid(char input);
extern void sendkey(uint8_t *characters,unsigned char char_len,uint8_t modifiers, int resend_count, int type_selected);
extern void msc_ram_init(void);
// extern int SDH_WriteMultiBlocks_async_check(int check);
extern int SDH_WriteMultiBlocks_async_finish(void);


extern void read_write_async_resp_david(bool usbd_msc_sector_return_value, int type, uint64_t sector, uint8_t *buffer, uint64_t length);

// Define drive as sd card
int ldnum = DEV_SD;

// #if async_writes
// volatile unsigned char sd_async_write_in_progress = 0;
// #endif

// // Define block size for use in callback and in setup
// uint32_t msc_blocksize;
// // Define block count in use of setup
// uint32_t msc_blockcount;
// // Define variable for use in write callback to define if it can be used or not
// bool msc_ro;
// // generaly unused
// bool msc_removable;
// // generaly unused
// bool msc_cdrom;


struct hid_msc main_msc;

// Refrence to fs object used to read and write in exfat
FATFS fs;
// Open file for use with the usb reader
// FIL fnew;



// Default text provided in the ini files; should be a working config




// used to determin if mass storage device is currently passed through
bool usb_started = false;






// Gpio device refrence for use in all gpio operations
struct bflb_device_s *gpio;

char LED_COLOR_RED = 1;		//0x001 -- Represents the color red in the set_led_color function
char LED_COLOR_BLUE = 2;	//0x010 -- Represents the color blue in the set_led_color function
char LED_COLOR_GREEN = 4;	//0x100 -- Represents the color green in the set_led_color function
uint64_t button_held_ms_special = 0;	//Equal to the amount of time the "special" button has been held down
bool button_pressed_special = false;	//Describes the previous state of the "special" button to trigger on press and release

/**
 * @brief Provide colors as a binary value, LED_COLOR_RED | LED_COLOR_BLUE will turn on both leds
 *
 * @param color char color made up of bits for red green and blue
 */
void set_led_color(char color) {
	bflb_gpio_reset(gpio, GPIO_PIN_21);
	bflb_gpio_reset(gpio, GPIO_PIN_20);
	bflb_gpio_reset(gpio, GPIO_PIN_23);
	
	if (color & LED_COLOR_RED)
		bflb_gpio_set(gpio, GPIO_PIN_23);
	if (color & LED_COLOR_BLUE)
		bflb_gpio_set(gpio, GPIO_PIN_20);
	if (color & LED_COLOR_GREEN)
		bflb_gpio_set(gpio, GPIO_PIN_21);
}


/**
 * @brief Sets up gpio pins as outputs for use with set_led_color
 */
void led_init(void) {
	bflb_gpio_init(gpio, GPIO_PIN_21, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
	bflb_gpio_init(gpio, GPIO_PIN_20, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
	bflb_gpio_init(gpio, GPIO_PIN_23, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
}

/**
 * @brief Sets up gpio pins as inputs (with pullups) for use with get_current_mode
 */
void mode_select_init(void) {
	bflb_gpio_init(gpio, GPIO_PIN_6, GPIO_INPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0);
	bflb_gpio_init(gpio, GPIO_PIN_7, GPIO_INPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0);
	bflb_gpio_init(gpio, GPIO_PIN_30, GPIO_INPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0);
	bflb_gpio_init(gpio, GPIO_PIN_31, GPIO_INPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0);
	bflb_gpio_init(gpio, GPIO_PIN_28, GPIO_INPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0);
}

/**
 * @brief Returns a unsigned char representing the current mode recived on gpio pins.
 */
unsigned char get_current_mode(void) {
	// Initiate mode with a value of 0, to allow for next operations
	unsigned char mode = 0;
	// Bitshift mode over one, and write binary value recived by pins in reverse
    mode = (mode<<1) | !bflb_gpio_read(gpio, GPIO_PIN_6);
    mode = (mode<<1) | !bflb_gpio_read(gpio, GPIO_PIN_7);
    mode = (mode<<1) | !bflb_gpio_read(gpio, GPIO_PIN_30);
    mode = (mode<<1) | !bflb_gpio_read(gpio, GPIO_PIN_31);

	// Recive special button status
	bool pin_status = !bflb_gpio_read(gpio, GPIO_PIN_28);

	// Check if in mode 0, and if special button has changed states
	if (pin_status^button_pressed_special && mode == 0) {
		if (pin_status) {
			// We can assume it was previously off, and here set button_held_ms_special to the current time
			button_held_ms_special = bflb_mtimer_get_time_ms();
		} else {
			// If the button was just released, we can mesure the amount of time it has been after it was pressed
			// If this value is over 5000ms, we trigger the temporary install mode, where it will partition the sd and add required files
			if ((bflb_mtimer_get_time_ms() - button_held_ms_special) > 5*1000) {
				mode = 255;
			}
		}
		// Store this value, so nothing repeats
		button_pressed_special = pin_status;
	}
	return mode;
}


/**
 * @brief Returns a parsed config or error message depending on if the passed file is readable / if durring reading it fails, souch events will return with err_code as non 0
 * 
 * @param path A path to the config file to attempt to parse
 */
PARSED_CONFIG parse_config(TCHAR *path) { 
	// Create a instance of PARSED_CONFIG to return
    PARSED_CONFIG config;

	// Copy the parsed path into the config
    strcpy(config.path,path);

	config.cd_img = false;
	config.bs = 512;
	config.cache_writes = false;
	config.size = 0;
	config.blockcount = 0;
	strcpy(config.name,"image.iso");
	strcpy(config.hid_file,"hid.txt");
	config.ro = false;
	config.err_code = 0;
	strcpy(config.selected_type,"image");


	// Make a refrence to a new file object, this will be used to store a refrence to the open config file
	FIL f_config;

	// Open the config file to the f_config refrence for reading, returning with a exit code, should a error occor
	config.err_code = f_open(&f_config, path, FA_OPEN_EXISTING | FA_READ);
	if (config.err_code != FR_OK) return config;


	// String buffer to record each line as we read over f_config
	TCHAR string_buff[150];

	// String buffer to record the type in a `type = data` keypair
	TCHAR type[150];
	// String buffer to record the data in a `type = data` keypair
	TCHAR data[150];

	// Loop over every line (or 150 char) in the config file
	while (f_gets(string_buff,150,&f_config)) {
		// Skip executing logic for lines starting with # or [, this is so comments can be made in the file
		if (string_buff[0] == '[' || string_buff[0] == '#') continue;

		// Remove any \n (newlines) that that are in the buffer, this also terminates the string at said \n 
		string_buff[strcspn(string_buff, "\n")] = 0;

		// Read from the input buffer with the `type` buffer as a string with no spaces, a space, equals, a space, and the rest of the lines data into the `data` buffer
		sscanf(string_buff,"%s = %[^\n]",type,data);


		// Using strcmp check if type in this itteration is equal to the type it needs
		// if it matches, parse by directly copying the string in the case of a string resource, or by parsing numbers in the case of others
		// if a boolean is required, the value 1 or 0 is used by casting a int read to a bool
		if (strcmp(type,"block_count") == 0) {sscanf(data,"%u",&config.blockcount);}
		if (strcmp(type,"name") == 0) {strcpy(config.name,data);}//config.name = data;}
		if (strcmp(type,"bs") == 0) {sscanf(data,"%u",&config.bs);}

		if (strcmp(type,"selected_type") == 0) {strcpy(config.selected_type,data);}
		if (strcmp(type,"hid_file") == 0) {strcpy(config.hid_file,data);}

		if (strcmp(type,"ro") == 0) {
			int tmp_ro_int;
			sscanf(data,"%u",&tmp_ro_int);
			config.ro = (bool) tmp_ro_int;
		}

		if (strcmp(type,"cache_writes") == 0) {
			int tmp_cache_writes_int;
			sscanf(data,"%u",&tmp_cache_writes_int);
			config.cache_writes = (bool) tmp_cache_writes_int;
		}

		if (strcmp(type,"cd_image") == 0) {
			int tmp_cd_image_int;
			sscanf(data,"%u",&tmp_cd_image_int);
			config.cd_img = (bool) tmp_cd_image_int;
		}

		
		
	}
	

	if (config.cd_img) {
		config.ro = true;
		config.bs = 2048; // CDS ONLY SUPPORT 2048
	}

	// Cant cache writes on a cd image or read only mode
	if (config.ro) 
		config.cache_writes = false;

	// store the total size needed as a qword, there is no other way to read this value, it is too long for sscanf to parse
    config.size = config.bs * config.blockcount;

    return config;
}

/**
 * @brief Generates all required image files, this will be ran after partitioning by the special key. Returns a error code should a error occor, 0 if otherwise
 *
 */
int generate_basic_image(void) {

	FIL f_generator;

	// HELP FILEs
	#if help_files
		f_mkdir("/sd/help_files");
		
		if (f_open(&f_generator, "/sd/help_files/ascii_to_hid.txt", FA_OPEN_ALWAYS | FA_WRITE) != FR_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Failed to open help file, ASCII_HID");
			return -1;
		}
		f_lseek(&f_generator,0);


		f_puts("Convert keys on the keyboard to hid codes\nhid : meaning",&f_generator);

		char temp_buffer[110] = ""; 

		int offset = 0;
		int str_length_of_next_itter=0;
		for (int i=0;i<255;i++) {
			str_length_of_next_itter = strlen(&help_file_keys_meanings[offset]);
			
			//100 - 8 - 2 // 90
			snprintf(temp_buffer,110,"\n%03d : %.95s",i,str_length_of_next_itter > 1 ? &help_file_keys_meanings[offset] : "N/A");
			f_puts(temp_buffer,&f_generator);

			offset += str_length_of_next_itter + 1;
		}

		f_close(&f_generator);


		if (f_open(&f_generator, "/sd/help_files/browser_console.html", FA_OPEN_ALWAYS | FA_WRITE) != FR_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Failed to open help file, UartConsole");
			return -1;
		}

		f_puts(help_file_uart_console,&f_generator);

		f_close(&f_generator);



		if (f_open(&f_generator, "/sd/help_files/commands.txt", FA_OPEN_ALWAYS | FA_WRITE) != FR_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Failed to open help file, Commands");
			return -1;
		}

		f_puts(help_file_commands,&f_generator);

		f_close(&f_generator);

	#endif

	// make the main images directory
	f_mkdir("/sd/images");

	// Define a file refrence, this is used for three things; No config file, or Opening / verifying program files, Opening / resizing image files
	// Refrence to the directory where the current index is held in
	char directory_loc_buff[14];
	// Refrence to the name of the config file, used for reading config
	char config_loc_buff[14+11];
	// Loop over all directorys that will be created / used
	for (int i = 1; i < 16; ++i) {
		// Define current itterations images directory and config files
		sprintf(directory_loc_buff,"/sd/images/%d",i);
		sprintf(config_loc_buff,"/sd/images/%d/config.ini",i);

		// Make a directory if missing
		f_mkdir(directory_loc_buff);

		// Define a info object and assing it to the stats of the config file, used to check if the file exists
		FILINFO info;
		int ret = f_stat(config_loc_buff,&info);

		// If the file dose not exist
		if ((ret == FR_OK && (info.fsize == 0)) || ret==FR_NO_FILE || ret==FR_NO_PATH) {
			LOG_I("No config file located; creating config: %s\r\n",config_loc_buff);

			// Create the config file
			int ret = f_open(&f_generator, config_loc_buff, FA_CREATE_ALWAYS | FA_WRITE);
			if (ret == FR_OK) {
				f_puts(DEFAULT_CONFIG_INI,&f_generator);
				f_sync(&f_generator);
				f_close(&f_generator);

				// Create a blank image file, or just open it if it already exists
				char image_loc_buff[14+10];
				sprintf(image_loc_buff,"/sd/images/%d/image.iso",i);
				
				f_open(&f_generator, image_loc_buff, FA_OPEN_ALWAYS | FA_WRITE);
				f_close(&f_generator);
			} else {
				set_led_color(LED_COLOR_RED);
				LOG_E("FAILED TO OPEN CONFIG: %s\r\n",config_loc_buff);
				return -1;
			}
		} else {
			// Check if the file actualy exists, or if some other diskio operation had a bug
			if (ret == FR_OK && (info.fsize != 0)) {
				// Run parse config to read info inside it
				PARSED_CONFIG config = parse_config(config_loc_buff);
				// Check config file was read properly
				if (config.err_code == 0 && strlen(config.selected_type) != 0) {
					bool type_valid = false;

					// Check if we are reading a hid file
					if (strcmp(config.selected_type,"hid") == 0) {
						type_valid = true;
						// Check to make sure the program path is not blank
						if (strlen(config.hid_file) != 0) {

							// Define and set a buffer containing the path to the hid program
							char program_loc_buff[14+1+40];
							sprintf(program_loc_buff,"/sd/images/%d/%s",i,config.hid_file);

							// Open and close the hid program, (creates the file if not existant)
							if (f_open(&f_generator, program_loc_buff, FA_OPEN_ALWAYS | FA_READ | FA_WRITE) != FR_OK) {
								set_led_color(LED_COLOR_RED);
								LOG_E("FAILED TO OPEN HID PROGRAM FILE: %s\r\n",program_loc_buff);
								return -1;
							}
							f_close(&f_generator);
						} else {
							set_led_color(LED_COLOR_RED);
							LOG_E("No hid program specified: %s\r\n",config_loc_buff);
							return -1;
						}
					}
					// Check if we are reading a image file
					if (strcmp(config.selected_type,"image") == 0) {
						type_valid = true;
						
						// Check to make sure the image file path is not blank
						if (strlen(config.name) != 0) {
							
							// Define and set a refrence to the image files location
							char image_loc_buff[14+1+40];
							sprintf(image_loc_buff,"/sd/images/%d/%s",i,config.name);

							// Open the image file
							if (f_open(&f_generator, image_loc_buff, FA_OPEN_ALWAYS | FA_READ | FA_WRITE) != FR_OK) {
								set_led_color(LED_COLOR_RED);
								LOG_E("FAILED TO OPEN IMAGE FILE: %s\r\n",config.name);
								return -1;
							}

							// Check and store the current config files specified size, and the actual image files current size
							FSIZE_t new_size = config.size;
							FSIZE_t old_size = f_size(&f_generator);
							
							// If the new size is 0, the user dose not care about size, if the sizes match, no resizing needs to be done
							LOG_D("Size_img: %d ; %d",new_size,old_size);

							if (new_size != 0 && new_size != old_size) {
								LOG_I("Force resize by:  %s\r\n",image_loc_buff);
								
								

								// If we are expanding the file
								if (new_size > old_size) {
									int ret_val = FR_OK;
									// Check if size is equal to 0, if so, we can do a quick expand!
									if (old_size == 0) {
										ret_val = f_expand(&f_generator,new_size,1);
										// if this fails, log it, but continue, we will try the slow method (this one only allocates continuous free blocks, not just random ones)
										if (ret_val != FR_OK) {
											
											set_led_color(LED_COLOR_RED);
											LOG_I("Resize-expand err: %d\r\n",ret_val);
										}
									}

									// If the previous method failed, or if we already have data, use the slow method of writing all 0's
									if (old_size != 0 || ret_val != FR_OK) {
										// Seek the open file to the end
										f_lseek(&f_generator,old_size);
										
										// Allocate a 4096 8-bit aligned memory block filled with 0's
										uint32_t *workbuf = malloc(4096);
										memset(workbuf, 0x00, 4096);

										// Define a Uint containing how much we need to write durring this itteration
										UINT bytes_writen;
										// Check the return value from the function to see if we did actualy write that many bites
										UINT bytes_to_write;

										// Int to contain how many full 4096 blocks we can write at once
										int loop_count = ((new_size-old_size)/4096); // floor this by assinging int
										
										// Write in 4096 chunks the max number of 4096 chunks used to resize to the specified amount
										bytes_to_write = 4096;
										for (int o=0; o<loop_count;++o) {
											ret_val = f_write(&f_generator,workbuf,bytes_to_write,&bytes_writen);
											if (ret_val != FR_OK) {
												set_led_color(LED_COLOR_RED);
												LOG_E("Resize-write err: %d\r\n",ret_val);
											}
										}
										// Store the remaining bytes (we illimated all 4096 block chunks, so just use mod)
										bytes_to_write = (new_size-old_size)%4096;

										// Write the last required non 4096 size sections to the file
										ret_val = f_write(&f_generator,workbuf,bytes_to_write,&bytes_writen);

										if (ret_val != FR_OK) {
											set_led_color(LED_COLOR_RED);
											LOG_E("Resize-write err (2): %d\r\n",ret_val);
										}
										
										// Free the allocated 4096 bite buffer
										free(workbuf);
									}
								}
								// Check if we have to shrink the file
								if (old_size > new_size) {
									int ret_val = FR_OK;
									
									// Seek to the new size, this will always be before the end
									ret_val = f_lseek(&f_generator,new_size);
									if (ret_val != FR_OK) {
										set_led_color(LED_COLOR_RED);
										LOG_E("Shrink-write err (1): %d\r\n",ret_val);
									}
									// Truncate the file to the currently seeked location
									ret_val = f_truncate(&f_generator);
									if (ret_val != FR_OK) {
										set_led_color(LED_COLOR_RED);
										LOG_E("Shrink-write err (2): %d\r\n",ret_val);
									}
									
								}

								LOG_I("RESIZE INFO: new-size:%llu, old-size:%llu, after-size:%llu, index:%d\r\n",new_size,old_size,f_size(&f_generator),i);
							}
							// Close and save edited file
							f_sync(&f_generator);
							f_close(&f_generator);
						} else {
							set_led_color(LED_COLOR_RED);
							LOG_E("Image config failed: %s\r\n", config_loc_buff);
							return -1;
						}
					}
					// if flashing, just deal with it, we dont have to do anything special
					if (strcmp(config.selected_type,"flash") == 0) {
						type_valid = true;
					}

					if (!type_valid) {
						set_led_color(LED_COLOR_RED);
						LOG_E("Incorrect selected type in file: %s\r\n", config_loc_buff);
						return -1;
					}
				} else {
					set_led_color(LED_COLOR_RED);
					LOG_E("Failed to load config from file: %s\r\n", config_loc_buff);
					return -1;
				}
			} else {
				set_led_color(LED_COLOR_RED);
				LOG_E("Failed to load config file size :( disk io error (%d): %s\r\n",ret,config_loc_buff);
				return -1;
			}
		}
		
	}
	return 0;
}




#if flash_mode 
unsigned char flash_mode_data_block_1p1[219] = {
	// Offset 0x00000000 to 0x000000DA
	0xEB, 0x58, 0x90, 0x6D, 0x6B, 0x66, 0x73, 0x2E, 0x66, 0x61, 0x74, 0x00,
	0x02, 0x01, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00,
	0x27, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x25, 0x31, 0x01, 0x00,
	0x59, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x29, 0xA5, 0x37, 0x05, 0xF2, 0x4E,
	0x4F, 0x20, 0x4E, 0x41, 0x4D, 0x45, 0x20, 0x20, 0x20, 0x20, 0x46, 0x41,
	0x54, 0x33, 0x32, 0x20, 0x20, 0x20, 0x0E, 0x1F, 0xBE, 0x77, 0x7C, 0xAC,
	0x22, 0xC0, 0x74, 0x0B, 0x56, 0xB4, 0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10,
	0x5E, 0xEB, 0xF0, 0x32, 0xE4, 0xCD, 0x16, 0xCD, 0x19, 0xEB, 0xFE, 0x54,
	0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x6E, 0x6F, 0x74, 0x20, 0x61,
	0x20, 0x62, 0x6F, 0x6F, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x20, 0x64, 0x69,
	0x73, 0x6B, 0x2E, 0x20, 0x20, 0x50, 0x6C, 0x65, 0x61, 0x73, 0x65, 0x20,
	0x69, 0x6E, 0x73, 0x65, 0x72, 0x74, 0x20, 0x61, 0x20, 0x62, 0x6F, 0x6F,
	0x74, 0x61, 0x62, 0x6C, 0x65, 0x20, 0x66, 0x6C, 0x6F, 0x70, 0x70, 0x79,
	0x20, 0x61, 0x6E, 0x64, 0x0D, 0x0A, 0x70, 0x72, 0x65, 0x73, 0x73, 0x20,
	0x61, 0x6E, 0x79, 0x20, 0x6B, 0x65, 0x79, 0x20, 0x74, 0x6F, 0x20, 0x74,
	0x72, 0x79, 0x20, 0x61, 0x67, 0x61, 0x69, 0x6E, 0x20, 0x2E, 0x2E, 0x2E,
	0x20, 0x0D, 0x0A
};

unsigned char flash_mode_data_block_1p2[2] = {
	0x55, 0xAA
};

unsigned char flash_mode_data_block_2p1[4] = {
	0x52, 0x52, 0x61, 0x41
};

unsigned char flash_mode_data_block_2p2[9] = {
	0x72, 0x72, 0x41, 0x61, 0x52, 0x2C, 0x01, 0x00, 0x02
};

unsigned char flash_mode_data_block_32p1[12] = {
	// Offset 0x00004000 to 0x0000400B
	0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xF8, 0xFF, 0xFF, 0x0F
};


int flash_read_block(uint32_t sector_, uint8_t *buffer, uint32_t length) {
	// sector_read;
	uint64_t start_sector = sector_;
	uint64_t current_sector;

	// printf("flash_read1\r\n");
	
	// printf("Read sectors aaaa: %d     %d\r\n",start_sector,length);
	
	memset(buffer,0,length);

	// printf("flash_read2\r\n");

	// for (int i=0;i<(real_length/512);i++) {

	// }

	// printf("Read sectors: %d     %d\r\n",start_sector,length);

	if (length < 512) {
		printf("0 length read???\r\n");
		return 0;
	}

	for (current_sector=start_sector;current_sector<((length/512)+start_sector);current_sector++) {
		uint8_t *current_buffer = &buffer[(current_sector-start_sector)*512];

		// printf("Read current sector: %d\r\n",current_sector);

		if (current_sector == 0 || current_sector == 6) {
			memcpy(&current_buffer[0],flash_mode_data_block_1p1,219);
			// memset(&current_buffer[254],0x55,1);
			// memset(&current_buffer[255],0xAA,1);
			memcpy(&current_buffer[510],flash_mode_data_block_1p2,2);
		}
		if (current_sector == 1 || current_sector == 7) {
			memcpy(&current_buffer[0],flash_mode_data_block_2p1,4);
			memcpy(&current_buffer[484],flash_mode_data_block_2p2,9);

			memcpy(&current_buffer[510],flash_mode_data_block_1p2,2); // reused :)
		}
		if (current_sector == 32 || current_sector == 633) {
			memcpy(&current_buffer[0],flash_mode_data_block_32p1,12);
		}

		

		
	}

	// printf("flash_read3\r\n");
	return 0;
	// memcpy
}



/*
int flash_read_block(uint32_t sector_, uint8_t *full_buffer, uint32_t real_length) {
	uint64_t start_sector = sector_;
	uint64_t current_sector;

	printf("flash_read1\r\n");
	
	memset(&full_buffer,0,real_length);

	printf("flash_read2\r\n");

	// for (int i=0;i<(real_length/512);i++) {

	// }

	printf("Read sectors: %d     %d\r\n",start_sector,real_length);

	if (real_length < 512) {
		return 0;
	}

	for (current_sector=start_sector;current_sector<((real_length/512)+start_sector);current_sector++) {
		uint8_t *current_buffer = &full_buffer[(current_sector-start_sector)*512];

		printf("Read current sector: %d\r\n",current_sector);

		if (current_sector == 0 || current_sector == 6) {
			memcpy(&current_buffer[0],&flash_mode_data_block_1p1,219);
			// memset(&current_buffer[254],0x55,1);
			// memset(&current_buffer[255],0xAA,1);
			memcpy(&current_buffer[510],&flash_mode_data_block_1p2,2);
		}
		if (current_sector == 1 || current_sector == 7) {
			memcpy(&current_buffer[0],&flash_mode_data_block_2p1,4);
			memcpy(&current_buffer[484],&flash_mode_data_block_2p2,9);

			memcpy(&current_buffer[510],&flash_mode_data_block_1p2,2); // reused :)
		}
		if (current_sector == 32 || current_sector == 633) {
			memcpy(&current_buffer[0],&flash_mode_data_block_32p1,12);
		}

		

		
	}

	printf("flash_read3\r\n");
	return 0;
	// memcpy
}*/

int flash_write_block(uint32_t sector_, uint8_t *buffer, uint32_t length) {
	// printf("flash_write1\r\n");

	// printf("Block in hex:\r\n");
	// for (int j=0; j<16; j++) {
	// 	for (int i=0; i<16; i++) {
	// 		printf("%02x ", buffer[i + j*16]);
			
	// 	}
	// 	printf("\r\n");
	// }
	// printf("\r\n\r\n");

	uint8_t *psram_buf = (uint8_t *)BFLB_PSRAM_BASE;

	uint64_t start_sector = sector_;
	uint64_t current_sector;

	// printf("wrie flash size: %d\r\n",length);

	for (current_sector=start_sector;current_sector<((length/512)+start_sector);current_sector++) {
		UF2_Block *current_buffer = (UF2_Block*) &buffer[(current_sector-start_sector)*512];

		//swap endianness because STUPID computer
		// endian_swap_uint32_t_self_modify(&current_buffer->magicStart0);
		// endian_swap_uint32_t_self_modify(&current_buffer->magicStart1);
		// endian_swap_uint32_t_self_modify(&current_buffer->magicEnd);

		// printf("magicStart0: %d")


		//0x00004000 - MD5 checksum present - see below
		// 0x00008000 - extension tags present - see below


		if (current_buffer->magicStart0 == endian_swap_uint32_t(0x0A324655) ||
		current_buffer->magicStart1 == endian_swap_uint32_t(0x9E5D5157) ||
		current_buffer->magicEnd == endian_swap_uint32_t(0x0AB16F30)) {
			if (
				!(current_buffer->flags & endian_swap_uint32_t(0x00000001)) &&
				!(current_buffer->flags & endian_swap_uint32_t(0x00001000))
				) {
					// if ((current_buffer->flags & endian_swap_uint32_t(0x00002000))) {
					// 	if (current_buffer->fileSize != 0x64DA64DA) {
					// 		// WRONG CHIP
					//      	// Dont flash here :(
					// 		printf("Wrong chip selected\r\n");
					// 		flash_correct_board = false;
					// 		break;
					// 	}
					// }

				// skip broken blocks
				if (current_buffer->numBlocks < current_buffer->blockNo) {
					printf("outside flash?\r\n");
					continue;
				}

				printf("Flashing block: %d of %d\r\n",current_buffer->blockNo,current_buffer->numBlocks);


				memcpy(&psram_buf[current_buffer->targetAddr],
				&current_buffer->data,current_buffer->payloadSize);



				uint32_t current_end_addr = current_buffer->targetAddr+current_buffer->payloadSize;

				if (current_end_addr > flash_total_length)
					flash_total_length = current_end_addr;


				

			} else {
				printf("Bad flags!\r\n");
			}

			if (current_buffer->numBlocks-1 == current_buffer->blockNo) {
				printf("Flash done?\r\n");

				flash_done = true;
				break;
			}

		} else {
			//Another thing to concider here, is that thre should be some failed numbers
			//Because the os should write some fatfs things,
			//But like 95-98% of the writes should have the magic header
			// ye I had this disabled before because of that and fat fs corrections when it starts, but when all fail, that means smth else is bad

			// just sent the bin
			// printf("failed majic numbers %d\r\n",current_buffer->magicStart0);
		}
	
	}
	return 0;
}


void start_flash_function(void) {

	flash_total_length = 0x0;
	flash_correct_board = true;
	flash_done = false;
	// memset((uint8_t *)BFLB_PSRAM_BASE,0,9999);


	LOG_I("Starting in flashing mode :)\r\n");

	sector_read = &flash_read_block;
	sector_write = &flash_write_block;

	main_msc.ro = false;
	main_msc.blocksize = 512;
	main_msc.cd_img = false;
	main_msc.blockcount = 40000000 / main_msc.blocksize;

	//Init USB
	msc_ram_init();
	usb_started = true;

	
}


#endif


/**
 * @brief Refrences a open file to read and supply data from rather than a real block, this will be defined in sector_read if mode is not 0
 * 
 * @param sector The sector offset
 * @param buffer The buffer to write data to
 * @param length The number of bytes to read
 */
int fs_read_file(uint32_t sector_, uint8_t *buffer, uint32_t length) {
	uint64_t sector = sector_;
	// printf("fat read request\r\n");



	// seek into the open file, at the location multiplyed by 4096, the sector size (<<9 == 4096)
#if !async_fs_writes

	int ret = 0;
	UINT fnum;	


	if (main_msc.write_cacheing == false) {
		f_lseek(&main_msc.read_file, (sector*main_msc.blocksize));
		// Read data specified
		ret = f_read(&main_msc.read_file, buffer, length, &fnum);
	} else {
		// Get the number of blocks needed, based on the length and block size
		uint8_t blocks_to_read = (length/main_msc.blocksize);
		// Seek to the offset of the start sector in the cache table
		f_lseek(&main_msc.cache_table_file,sector*sizeof(DWORD));
		// Create and store a cache offset table for just this data, based off the cache table
		DWORD cache_offset[blocks_to_read];
		ret |= f_read(&main_msc.cache_table_file,&cache_offset,blocks_to_read*sizeof(DWORD),&fnum);

		// We cant do our operations if there are no bytes to read
		if (blocks_to_read == 0) return 1;
		// Define a variable to store how many previous itterations had blocks in order (could be a uchar)
		int blocks_in_a_row = 0;
		for (int i = 0; i < blocks_to_read; i++) {
			// Skip the first itteration, beacuse we will need to read the previous itteration in the operations to folow
			if (i > 0) {
				// If the previous and current ones are in order, mark as souch, and continue untill this is not the case
				if ((cache_offset[i-1]+1 == cache_offset[i]) || (cache_offset[i-1] == 0 && cache_offset[i] == 0)) { // TODO DETERMIN IF THIS IS STUPID
					blocks_in_a_row += 1;
				} else {
					// Check to see if they were in a row on the cache, or origonal file
					if (cache_offset[i-1] != 0) {
						// Seek to the offset inside the cache file as determined by the first block in the blocks in a row
						f_lseek(&main_msc.cache_file,(cache_offset[i-blocks_in_a_row-1]*main_msc.blocksize));
						// Read off the blocks in a row to their buffers
						ret |= f_read(&main_msc.cache_file, buffer + ((i-blocks_in_a_row-1)*main_msc.blocksize), ((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);
					} else {
						// Seek in the origonal file using the offset of the sector where we started reading
						f_lseek(&main_msc.read_file,(sector+(i-blocks_in_a_row-1))*main_msc.blocksize);
						// Read off the blocks required
						ret |= f_read(&main_msc.read_file, buffer + ((i-blocks_in_a_row-1)*main_msc.blocksize),((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);
					}

					// This block is diffrent, mark as souch
					blocks_in_a_row = 0;
				}
			}
		}

		// Same as inside loop, just dont for reamining blocks
		if (cache_offset[blocks_to_read-1] != 0) {
			f_lseek(&main_msc.cache_file,(cache_offset[blocks_to_read-blocks_in_a_row-1]*main_msc.blocksize));
			ret |= f_read(&main_msc.cache_file, buffer + ((blocks_to_read-blocks_in_a_row-1)*main_msc.blocksize), ((blocks_in_a_row + 1)*main_msc.blocksize), &fnum); 
		} else {
			f_lseek(&main_msc.read_file,(sector+(blocks_to_read-blocks_in_a_row-1))*main_msc.blocksize);
			ret |= f_read(&main_msc.read_file, buffer + ((blocks_to_read-blocks_in_a_row-1)*main_msc.blocksize),((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);
		}

	}
	

	
	if (ret != FR_OK) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Read Ret: %d, fnum: %u\r\n", ret, fnum);
	}

	return ret;
#else
	buffer_fatfs_function.operation = 1;
	buffer_fatfs_function.length = length;
	buffer_fatfs_function.sector = sector;
	buffer_fatfs_function.buffer = NULL; // unneeded fro read
	return 2;
#endif

	
}

/**
 * @brief Refrences a real block, this will be defined in sector_read if mode is 0
 * 
 * @param sector The sector offset
 * @param buffer The buffer to write data to
 * @param length The number of bytes to read
 */
int msc_read_block(uint32_t sector_, uint8_t *buffer, uint32_t length) {
	uint64_t sector = sector_;
	int ret;

#if async_writes
	// printf("reading....");
	// if (sd_async_write_in_progress == 1 && SDH_WriteMultiBlocks_async_check(0) != 0) {
	// 	uint64_t start_time = bflb_mtimer_get_time_ms();
	// 	// usbd_msc_send_csw(CSW_STATUS_CMD_FAILED);
	// 	ret = 1;
	// 	while (ret == 1 && (bflb_mtimer_get_time_ms() - start_time <= 100000))
	// 		ret = SDH_WriteMultiBlocks_async_check(1);

	// 	if (ret == 2 || (bflb_mtimer_get_time_ms() - start_time > 100000)) {
	// 		printf("read errrrrrrr :( %d aa  %ull",ret,bflb_mtimer_get_time_ms() - start_time);
	// 		return 1;
	// 	}
	// }
	// if (sd_async_write_in_progress == 1)
	// 	SDH_WriteMultiBlocks_async_check(2);
	// sd_async_write_in_progress = 0;
	ret = SDH_WriteMultiBlocks_async_finish();
	if (ret != FR_OK) {
		set_led_color(LED_COLOR_RED);
		LOG_E("last async write failed: %d\r\n", ret);
		return ret;
	}
#endif

	// Run disk_read operation to do a read on the sd card
	ret = disk_read(ldnum, buffer, sector, length/main_msc.blocksize);
	if (ret != FR_OK) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Read Ret: %d\r\n", ret);
	}

	return ret;
}

/**
 * @brief Refrences a open file to write to rather than a real block, this will be defined in sector_write if mode is not 0
 * 
 * @param sector The sector offset
 * @param buffer The buffer of data
 * @param length The number of bytes to write
 */
int fs_write_file(uint32_t sector_, uint8_t *buffer, uint32_t length) {
	uint64_t sector = sector_;
	
// printf("fat write request\r\n");
#if !async_fs_writes

	int ret = 0;
	UINT fnum;


	// Check if we can directly read off the file, if so, do it
	if (main_msc.write_cacheing == false) {
		// seek into the open file, at the location multiplyed by 4096, the sector size (<<9 == 4096)
		f_lseek(&main_msc.read_file, (sector*main_msc.blocksize));

		// Write into the file the data with length specified
		ret = f_write(&main_msc.read_file, buffer, length, &fnum);
	} else {
		
		// Ok, complex logic, because idfk

		// Get the count of blocks we need to write into the cache
		uint8_t blocks_to_write = (length/main_msc.blocksize);

		// Seek into the cache_table_file at the location of the sector we should start at
		f_lseek(&main_msc.cache_table_file,sector*sizeof(DWORD));
		// Create two buffers, one cache_offset will be able to be changed, cache_offset_origonal should not be modified, if cache_offset is changed, the disk will be modified
		DWORD cache_offset[blocks_to_write];
		DWORD cache_offset_origonal[blocks_to_write];

		// Read the cache offsets into cache_offset, then copy it to the backup cache_offset_origonal
		ret |= f_read(&main_msc.cache_table_file,&cache_offset,blocks_to_write*sizeof(DWORD),&fnum);
		memcpy(&cache_offset_origonal,&cache_offset,blocks_to_write*sizeof(DWORD));


		// Unneeded, but here anyway, 0 size will legitimatly not work, and previous logic ensures that each read request is % blocksize
		if (blocks_to_write == 0) return 1;

		// Store the number of blocks previously in a row, this is used to optimize disk io operations (could be a uchar)
		int blocks_in_a_row = 0;
		for (int i = 0; i < blocks_to_write; i++) {
			// If the offset is currently in the origonal file, 0, assing it a new index
			if (cache_offset[i] == 0) {
				main_msc.write_cacheing_length += 1;
				cache_offset[i] = main_msc.write_cacheing_length;
			}
			// On every itteration after the first we run this code; we cant run on first because we have to compare the previous itteration
			if (i > 0) {
				// Check if we are going to write in a row, if so, add it to the count
				if (cache_offset[i-1]+1 == cache_offset[i]) {
					blocks_in_a_row += 1;
				} else {
					// If not, seek into the cache file at the start of the blocks that are in a row
					f_lseek(&main_msc.cache_file,(cache_offset[i-blocks_in_a_row-1]*main_msc.blocksize));
					// Write the data from the previous, in a row, blocks, instead of multiple disk io operations
					ret |= f_write(&main_msc.cache_file, buffer + ((i-blocks_in_a_row-1)*main_msc.blocksize), ((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);
			
					// We are not the same as previous, we need to reset this 
					blocks_in_a_row = 0;
				}
			}
		}

		// If cache_offset got changed, mark that it changed (this can be optimized to a boolean)
		if (memcmp(&cache_offset_origonal,&cache_offset,blocks_to_write*sizeof(DWORD)) != 0) {
			// Seek again into the correct offset for the cache table
			f_lseek(&main_msc.cache_table_file,sector*sizeof(DWORD));
			// Write the exact same way we read from it
			ret |= f_write(&main_msc.cache_table_file,&cache_offset,blocks_to_write*sizeof(DWORD),&fnum);
		}
		// Re execute code for similar / blocks in a row, because the previous block needs to be writen for us to be able to get all them writen
		f_lseek(&main_msc.cache_file,(cache_offset[blocks_to_write-blocks_in_a_row-1]*main_msc.blocksize));
					
		ret |= f_write(&main_msc.cache_file, buffer + ((blocks_to_write-blocks_in_a_row-1)*main_msc.blocksize), ((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);

	}

	// If any errors occored, exit and warn
	if (ret) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Write Ret: %d, fnum: %u\r\n", ret, fnum);
	}

    return (ret==0) ? 0 : 1;//ret;
#else
	//When the pc writes quickly, it overwrites whatever was in here
	// I fixed that by having it write the responce only after cloning this object, then freeing the memory later, here I will show u
	// printf("asymc!!! fat write request\r\n");
	buffer_fatfs_function.operation = 2;
	buffer_fatfs_function.length = length;
	buffer_fatfs_function.sector = sector;
	uint8_t *buffer_clone = malloc(length); // looks ok    - What i suspect is that when the pc
	memcpy(buffer_clone,buffer,length);
	buffer_fatfs_function.buffer = buffer_clone;//buffer; // cant use buffer as it will be freed, so make a copy of it :)
	return 2; //Is that the two meaning? it will wait untill something is returned?
	// 2 is the return value where I modded the library to just not send any messages if 2 is returned (normly even for writes it sends a (good) message) so now we can just send them our selfs later in the 

#endif
}


/**
 * @brief Refrences a real block to write to, this will be defined in sector_write if mode is 0
 * 
 * @param sector The sector offset
 * @param buffer The buffer of data
 * @param length The number of bytes to write
 */
int msc_write_block(uint32_t sector_, uint8_t *buffer, uint32_t length) {
	uint64_t sector = sector_;
	int ret;
// printf("write occoring :)");
#if async_writes
	// if (sd_async_write_in_progress == 1 && SDH_WriteMultiBlocks_async_check(0) != 0) {
	// 	uint64_t start_time = bflb_mtimer_get_time_ms();
	// 	// usbd_msc_send_csw(CSW_STATUS_CMD_FAILED);
	// 	ret = 1;
	// 	while (ret == 1 && (bflb_mtimer_get_time_ms() - start_time <= 100000))
	// 		ret = SDH_WriteMultiBlocks_async_check(1);

	// 	if (ret == 2 || (bflb_mtimer_get_time_ms() - start_time > 100000)) {
	// 		printf("write occorinasdasdadasasdg :)");
	// 		return 1;
	// 	}
	// }
	// if (sd_async_write_in_progress == 1)
	// 	SDH_WriteMultiBlocks_async_check(2);
	// sd_async_write_in_progress = 0;
	ret = SDH_WriteMultiBlocks_async_finish();
	if (ret != FR_OK) {
		set_led_color(LED_COLOR_RED);
		LOG_E("last async write failed: %d\r\n", ret);
		return ret;
	}

	

	// Preform a disk write operation on the sd card
	// printf("real write occoring :)");
	ret = disk_write(ldnum | 0b10000000, buffer, sector, length/main_msc.blocksize);//(length>>9));
#else
	ret = disk_write(ldnum, buffer, sector, length/main_msc.blocksize);//(length>>9));

#endif	

	// | 0b10000000 makes it run async, because I am using unused bits in that to make it async without moding all the source code

	if (ret) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Write Ret: %d\r\n", ret);
		return 1;
	}
// #if async_writes
// 	sd_async_write_in_progress = 1;
// #endif

	return 0;
}


/**
 * @brief Real callback from msc device, will call sector_read to handle the real read request
 * 
 * @param sector The sector offset
 * @param buffer The buffer to read data to
 * @param length The number of bytes to read
 */
int usbd_msc_sector_read(uint32_t sector, uint8_t *buffer, uint32_t length)
{
	// Length is not a multiple of 4096, that means its not at least 1 block, and invalid, close usb
	if ((length&0x1FF) != 0) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Attempting to read %u length, sector %u!\r\n",length, sector);
		return 1;
	}
	// If there is no processor, error and return 1
	if (sector_read == NULL) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Attempting to read with no function defined!\r\n");
		return 1;
	};
	
	// printf("flash_read0   %d\r\n",length);
	// Fail with 1, not some random value
	// printf("func gonna call %p %p\r\n", sector_read, sector_write);
	// printf("Func supposed to call %p %p\r\n", flash_read_block, flash_write_block);
	int sector_write_return_value = sector_read(sector, buffer, length);

	#if !async_fs_writes
		if (sector_write_return_value != 0)
			sector_write_return_value = 1;
	#endif

	
	if (sector_write_return_value == 1) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Sector read failed!\r\n");
		return 1;
	} 
	
	return sector_write_return_value; 
}

/**
 * @brief Real callback from msc device, will call sector_write to handle the real write request
 * 
 * @param sector The sector offset
 * @param buffer The buffer of data
 * @param length The number of bytes to write
 */
int usbd_msc_sector_write(uint32_t sector, uint8_t *buffer, uint32_t length) {

	// Length is not a multiple of 4096, that means its not at least 1 block, and invalid, close usb
	if ((length&0x1FF) != 0) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Attempting to write %u length!\r\n",length);
		return 1;
	}

	// If we are read only, we should never trigger this, because pc should never send a write request
	if (main_msc.ro == true) {
		set_led_color(LED_COLOR_RED);
		LOG_I("Attempting to write while write protected!!!!!!\r\n");
		return 1;
	}

	// If there is no function to run to execute this, its invalid, close the buffer
	if (sector_write == NULL) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Attempting to write with no function defined!\r\n");
		return 1;
	}

	// Fail with 1, not some random value

	// printf("flash_write0\r\n");

	int sector_write_return_value = sector_write(sector, buffer, length);
	if (sector_write_return_value == 1) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Sector write failed!\r\n");
		// return 1;
	} 
	
	return sector_write_return_value;//0;
}


// if (usbd_msc_sector_read_return_value == 0) {
//         usbd_ep_start_write(mass_ep_data[MSD_IN_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, transfer_len);
//     }






/**
 * @brief Runs hid programs at the path specified, returns on compleation, error, or when user changes input
 * 
 * @param type_selected The current type selected, provided by get_current_mode
 * @param path The buffer containing the bath to the program file
 */
void run_hid_program(int type_selected,char path[]) {
	// Define buffer sides, will error if lines are longer than this
	#define hid_program_line_max_length 150 //USED INSTEAD OF INT BECAUSE IT MAKES MY LIFE NOT PAIN (also inline assingments, because compiler optimizes away this)
	// File object that will hold the program
	FIL f_prog;
	// Open the program file, and error if it dose not exist, or cant be read
	if (f_open(&f_prog, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
		set_led_color(LED_COLOR_RED);
		LOG_E("FAILED TO OPEN PROGRAM: %s\r\n",path);
		return;
	}

	// Define a buffer to hold each line that is read from the file
	TCHAR command_buff[hid_program_line_max_length] = {0};

	// Buffer to hold the opcode of the instruction to execute
	TCHAR opcode[hid_program_line_max_length] = {0}; 
	// Buffer to hold the data to execute with the op code
	TCHAR data[hid_program_line_max_length] = {0};
	// Current line number in the file (unused, but intented for jump operations forward and backwards)
	unsigned int lines_read = 0;

	while (f_gets(command_buff,hid_program_line_max_length,&f_prog)) {
		// Increment line number
		lines_read += 1;
		// Ignore lines starting with # as they are comments
		if (command_buff[0] == '#') continue;
		
		// Check if the current mode changed from when program was started, and if so, exit all operation
		if (get_current_mode() != type_selected) return;
		// Remove trailing \n
		command_buff[strcspn(command_buff, "\n")] = 0;

		// Skip blank lines
		if (strlen(command_buff) == 0) continue;

		// Store the opcode from the command buffer, then any data proceding that as the data variable (removing space at the begining)
		sscanf(command_buff,"%s %[^\n]",&opcode,&data);

		LOG_I("[EXECUTION] op:%s data:%s",opcode,data);
		// Wait usb
		if (strcmp(opcode,"wu") == 0) {
			// Wait for the wait_for_usb_connection function to return true, or for type to change, where instruction callback will fall to above and reset
			while(!wait_for_usb_connection() && (get_current_mode() == type_selected));
		}
		// Led set
		if (strcmp(opcode,"ls") == 0) { 
			// Define variables to store red green and blue values read from data; these are only needed as there is no boolean type for sscanf
			unsigned int red_set = 0;
			unsigned int green_set = 0;
			unsigned int blue_set = 0;

			// Read data about red green and blue values into buffers defined above
			sscanf(data,"%u %u %u",&red_set, &green_set, &blue_set);

			// Use inline ifs to set the led color to the correct value (yes, I could have made it faster, but this is more readable)
			set_led_color(
				(red_set>0)*LED_COLOR_RED |
				(green_set>0)*LED_COLOR_GREEN |
				(blue_set>0)*LED_COLOR_BLUE
				);
		}
		// Keyboard string
		if (strcmp(opcode,"ks") == 0) {
			// Define variables for sscanf to read into
			unsigned int modifers = 0;
			unsigned int resend_count = 0;
			char ks_string[hid_program_line_max_length];

			// Read keyboard input
			sscanf(data,"%u %u %[^\n]",&modifers,&resend_count,&ks_string);

			// send keys with a max resend count of 50, as that takes 50ms to execute, and we dont want to pause execution for too long
			send_str(ks_string,(uint8_t) modifers,(uint8_t)(resend_count > 50 ? 50 : resend_count),type_selected);
		}
		// Keyboard input
		if (strcmp(opcode,"ki") == 0) { 
			// Variable to store wether the user is using keyboard codes or ascii chars
			unsigned int type_key = 0;
			// Variable to store the number of times it should resend this input (reliability)
			unsigned int repeat_count = 0;
			// Variable to store all 6 (max) key presses a single request can send at once
			uint8_t keys_incoming[6] = {0};
			// Variable to store the modifiers entered by the user (shift / win / control)
			unsigned int modifiers = 0;
			// First check the type specified, because latter operations change depending on it.
			sscanf(data,"%u",&type_key);
			// If the user entered 0, read as keyboard codes
			if (type_key == 0) {
				// Define a temp buffer to store the codes in (as ints)
				unsigned int keys_incoming_tmp[6] = {0};
				sscanf(data,"%u %u %u %u %u %u %u %u %u",&type_key,&repeat_count,&modifiers
				,&keys_incoming_tmp[0]
				,&keys_incoming_tmp[1]
				,&keys_incoming_tmp[2]
				,&keys_incoming_tmp[3]
				,&keys_incoming_tmp[4]
				,&keys_incoming_tmp[5]
				);

				// Cast the ints to uint8_t, the required type and store
				for (int i=0;i<6;i++) {
					keys_incoming[i] = (uint8_t) keys_incoming_tmp[i];
				}
			}
			// If the user entered 1, read as ascii
			if (type_key == 1) {
				// Define a ascii buffer to read to
				char keys_incoming_tmp[6] = {0};
				sscanf(data,"%u %u %u %c %c %c %c %c %c",&type_key,&repeat_count,&modifiers
				,&keys_incoming_tmp[0]
				,&keys_incoming_tmp[1]
				,&keys_incoming_tmp[2]
				,&keys_incoming_tmp[3]
				,&keys_incoming_tmp[4]
				,&keys_incoming_tmp[5]
				);
				// Convert the ascii chars to keyboard codes, remove all caps (even on special chars)
				for (int i=0;i<6;i++) {
					keys_incoming[i] = ascii_to_hid(keys_incoming_tmp[i]);
				}
			}

			// Send qued keypresses
			sendkey(keys_incoming,6,modifiers,repeat_count,type_selected);

		}
		// Mouse input
		if (strcmp(opcode,"mi") == 0) {
			// Define a buffer for the 3 signed values (x y and wheel)
			int mouse_tmp_ints[3] = {0};
			// Define a buffer for the unsinged value (buttons)
			unsigned int mouse_buttons = 0;
			// Copy data into singed and unsinged buffers
			sscanf(data,"%d %d %d %u",
			&mouse_tmp_ints[0],
			&mouse_tmp_ints[1],
			&mouse_tmp_ints[2],
			&mouse_buttons);
			// Cast values to appropriate types and store in mouse_cfg object
			mouse_cfg.x = (int8_t) mouse_tmp_ints[0];
			mouse_cfg.y = (int8_t) mouse_tmp_ints[1];
			mouse_cfg.wheel = (int8_t) mouse_tmp_ints[2];
			mouse_cfg.buttons = (uint8_t) mouse_buttons;
			// Send mouse packet to usb
			sendmouse(mouse_cfg,type_selected);
		}
		// Hid init
		if (strcmp(opcode,"hi") == 0) {
			hid_init();
		}
		// Hid disconnect
		if (strcmp(opcode,"hd") == 0) {
			hid_disconnect();
		}
		// General sleep
		if (strcmp(opcode,"gs") == 0) {
			// Store a value for how long to sleep based off user input
			unsigned int sleep_time = 0;
			sscanf(data,"%u",&sleep_time);

			// record start time
			uint64_t start_time = bflb_mtimer_get_time_ms();
			// while loop wasteing cycles untill the time has passed, or state has changed
    		while ((bflb_mtimer_get_time_ms() - start_time < sleep_time) && (get_current_mode() == type_selected)) {}
		}
		
		// Conditional jump
		if (strcmp(opcode,"cj") == 0) {
			memset(&opcode,0,hid_program_line_max_length);
			if (!bflb_gpio_read(gpio, GPIO_PIN_28)) {
				strcpy((char *) &opcode,"gj");
				// opcode = "gj";
			}
		}
		// General jump
		if (strcmp(opcode,"gj") == 0) {
			// Store a value for how long for to jump
			int jump_offset = 0;
			sscanf(data,"%d",&jump_offset);
			// Subtract 1 so gj 0 reruns this line
			jump_offset -= 1;


			// Define a variable for exiting due to fs errors inside for loops
			bool needs_to_exit = false;

			// Check if we are jumping forward
			if (jump_offset > 0) {
				// Consume the forward amount of lines
				for (int jump_count = 0;jump_count < jump_offset;jump_count++) {
					if (f_gets(command_buff,hid_program_line_max_length,&f_prog) == 0) {
						needs_to_exit = true;
						break;
					}
				}
				if (needs_to_exit) break;
				// Add the lines read
				lines_read += jump_offset;
			}
			if (jump_offset < 0) {
				// Jump to offset 0
				QWORD tmpp = 0;
				if (f_lseek(&f_prog,tmpp) != 0) {break;}
				// Jump forward (lines_read + jump_offset) lines
				for (int jump_count = 0;jump_count < (lines_read + jump_offset);jump_count++) {
					if (f_gets(command_buff,hid_program_line_max_length,&f_prog) == 0) {
						// LOG_I("testaa\r\n");
						needs_to_exit = true;
						break;
					}
				}
				if (needs_to_exit) break;

				// I dont think we add lines, but idfk
				// Set to the location we jumped to
				lines_read = (lines_read + jump_offset);
			}
		}
		LOG_I("    [FINISHED]\r\n");
	}
	// Call disconnect incase user forgot to :skull:
	hid_disconnect();
}


/**
 * @brief Resets the usb and disables all handles
 * 
 */
void run_usbdeinit(void) {
	// Check if currently running
	if (usb_started) {
		// Store that the usb is not running
		usb_started = false;
		// Disable handlers
		usbd_event_reset_handler();
		// Reset usb
		usbd_deinitialize();
	}
}

/**
 * @brief Resets the usb and disables all handles
 * 
 * @param type_selected The current selected mode by the user (255 is format)
 */
void run_usbinit(int type_selected) {
	//To get drive size
	uint64_t fsize;

	// Define default parameters that will be used to not have to reassing later, possiably causing errors
	main_msc.ro = false;
	main_msc.blocksize = 512;
	
	main_msc.write_cacheing_length = 0;
	main_msc.write_cacheing = false;
	main_msc.cd_img = false;

	// LOG_I("Starting with type %d",type_selected);
	

	// If we are in repartition / repair mode
	if (type_selected == 255) {
		// Set led to say we are working
		set_led_color(LED_COLOR_BLUE);
		// Ignore sector reads and writes
		sector_read = NULL;
		sector_write = NULL;
		
		// temp return value holder, so we dont have to do inline checks
		int ret = 0;
		// Try to mouse the sd card
		ret = f_mount(&fs, "/sd", 1);
		// If the sd card wont mount, repartition
		if (ret != FR_OK) {
			// Define info used to partition the device
			MKFS_PARM fs_para = {
			    .fmt = FM_EXFAT,     /* Format option (FM_FAT, FM_FAT32, FM_EXFAT and FM_SFD) */
			    .n_fat = 1,          /* Number of FATs */
			    .align = 0,          /* Data area alignment (sector) */
			    .n_root = 1,         /* Number of root directory entries */
			    .au_size = 512*1024//*32, /* Cluster size (byte) */
			};

			// Allocate a 4096 byte working buffer for partitionign
			uint32_t *workbuf = malloc(4096);
			
			// Partition with the parameters, and a 4096 byte workbuff
			ret = f_mkfs("/sd", &fs_para, workbuf, 4096);
			
			// Free used workbuff, its useless
			free(workbuf);

			// If we failed to partiton exit
			if (ret != FR_OK) {
				set_led_color(LED_COLOR_RED);
				LOG_E("fail to make filesystem %d\r\n", ret);
				return;
			}

			// Mount the partition, make sure it works
			ret = f_mount(&fs, "/sd", 1);
			if (ret != FR_OK) {
				set_led_color(LED_COLOR_RED);
				LOG_E("Mounting after making fs failed %d\r\n", ret);
				return;
			}

			// Generate the required files for operation / check them
			generate_basic_image();

			// Unmount the sd card when done
			f_unmount("/sd");

			// Set back to normal type 0, dev mode
			// Nvm, return as to update back to devmode and not start twice :D
			// type_selected = 0;
			return; 
		} else {
			// Just make the required image, then be done
			generate_basic_image();
			// Unmount the sd card when done
			f_unmount("/sd");
			// Set back to normal type 0, dev mode
			// type_selected = 0;
			// Nvm, return as to update back to devmode and not start twice :D
			return; 
		}
	}

	// Set led color to green, beacuse so far everythings good
	set_led_color(LED_COLOR_GREEN);

	if (type_selected == 0) {

		LOG_I("Dev mode\r\n");
		LBA_t sector_count; // only needs to be a DWORD
		WORD sector_size;


		// Run internal method starting sd card
		if (disk_initialize(ldnum) != RES_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Unable to start sd card!\r\n");
			return;
		}
		// Get disk size, in sectors
		// This can never fail (source just says ok no matter what), but add sanity checks anyway 
		if (disk_ioctl(ldnum, GET_SECTOR_COUNT, &sector_count) != RES_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Unable to get sd card size!\r\n");
			return;
		}
		if (disk_ioctl(ldnum, GET_SECTOR_SIZE, &sector_size) != RES_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Unable to get sd card size!\r\n");
			return;
		}
		// store disk size for block count use below
		main_msc.blocksize = sector_size;
		// fsize = sz_drv * 512;
		fsize = sector_count*sector_size;//<<9;
		
		LOG_I("Disk size: %u\r\n", fsize);//, sector_count);

		// If the drive has a 0 size, we cant read clearly
		if (fsize == 0) {
			sector_read = NULL;
			sector_write = NULL;
			set_led_color(LED_COLOR_RED);
			LOG_E("Drive 0 size; exiting\r\n");
			f_close(&main_msc.read_file);
			return;
		}
	
		//Setup sector_read and write for devmode
		sector_read = &msc_read_block;
		sector_write = &msc_write_block;
	} else {

		// Store value for errors and later checks
		int ret = 0;
	
		LOG_I("Access mode\r\n");
			
		// Attempt to mount sd card
		ret = f_mount(&fs, "/sd", 1);
		
		if (ret != FR_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("fail to mount filesystem,error= %d\r\n", ret);
			return;
		}
		LOG_I("FileSystem cluster size:%d-sectors (%d-Byte)\r\n", fs.csize, fs.csize * 512);


		// Store buffer for the config files location
		char config_loc_buff[14+11];
		// Store buffer for the image files location
		char image_loc_buff[14+1+40];
		
		// Store the config file location into the buffer
		sprintf(config_loc_buff,"/sd/images/%d/config.ini",type_selected);

		// Parse the config located at the config file location buffer
		PARSED_CONFIG config = parse_config(config_loc_buff);


		// moved to normal spot, not sure why it was here?
		// main_msc.cd_img = config.cd_img;

		// // Cant write to a cd image
		// if (config.cd_img) {
		// 	// config.bs = 2048;
		// 	// MOVED CHANGING CONFIG BS TO PARSER, needed every where :|
		// }
		// MOVED WHOLE CHECK INTO PARSING CONFIG 
		// // Cant cache writes on a cd image or read only mode
		// if (config.ro) 
		// 	config.cache_writes = false;


		// Make sure it parsed
		if (config.err_code == 0) {
			// janky hack mate (droppin through like this)
			// Check if it is a hid file
			if (strcmp(config.selected_type,"hid") == 0) {
				// Make sure the program name is real
				if (strlen(config.hid_file) == 0) {
					set_led_color(LED_COLOR_RED);
					LOG_E("Bad HID program name in ini: %s\r\n",config_loc_buff);
					return;
				}

				LOG_I("Loaded HID program with name: %s\r\n",config.hid_file);
				// Disable reads and writes over usb
				sector_read = NULL;
				sector_write = NULL;

				//image_loc_buff should not prob be used, but I already allocated memory, and it matches for the most part :)
				// Store the location of the hid program file
				sprintf(image_loc_buff,"/sd/images/%d/%s\r\n",type_selected,config.hid_file);

				// Run the program
				run_hid_program(type_selected,image_loc_buff);

				LOG_I("Finished execution");

				// Return execution 
				return;
			}

#if flash_mode
			if (strcmp(config.selected_type,"flash") == 0) {
				// janky hack again :|
				// Setup USB stuff;

				start_flash_function();
				return;
			}
#endif

			// If the type is not a image, the next part dose not apply and only option is to return out
			if (strcmp(config.selected_type,"image") != 0) {
				set_led_color(LED_COLOR_RED);
				LOG_E("Bad type in config file: %s ; type: %s\r\n",config_loc_buff,config.selected_type);
				return;
			}


			// Drop through only if image

			// If image file path is not blank continue
			if (strlen(config.name) == 0) {
				set_led_color(LED_COLOR_RED);
				LOG_E("Bad file name in ini: %s\r\n",config_loc_buff);
				return;
			}
			
			// Store full file path 
			sprintf(image_loc_buff,"/sd/images/%d/%s\r\n",type_selected,config.name);
			LOG_I("Loaded config with image: %s\r\n",config.name);
		} else {
			set_led_color(LED_COLOR_RED);
			LOG_E("Failed to load ini: %s\r\n",config_loc_buff);
			return;
		}

		// enable Cd image if set
		main_msc.cd_img = config.cd_img;

		// Enable read only mode if avalable
		main_msc.ro = (config.ro!=0);

		// Open the file for reading by fs_read_file
		ret = f_open(&main_msc.read_file, image_loc_buff, FA_OPEN_APPEND | FA_READ | FA_WRITE);
		if (ret != FR_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Failed to open %s !\r\n", image_loc_buff);
			return;
		}
		// Store the size of the image passed
		fsize = f_size(&main_msc.read_file);

		// David testing stupid stuff with image file so it will work mby
		if ((fsize%config.bs) != 0) {
			LOG_I("File size not exact, lieing about size!");
			// Lie about sector count, because we need to pretend to be 1 block bigger if we are even 1 bit over a block, and int devision later will just deleat this rather than doing ceil, this prevents those cases
			fsize += config.bs-(fsize%config.bs);
		}

		LOG_I("file %s opened!, size: %u, sectors: %u\r\n", image_loc_buff, fsize, fsize/config.bs);//512);
		main_msc.blocksize = config.bs;
		// If the file is blank, then we cannot passthrough a 0 byte usb device, so just exit
		if (fsize == 0) {
			sector_read = NULL;
			sector_write = NULL;
			set_led_color(LED_COLOR_RED);
			LOG_E("File 0 size; closing file %s\r\n",image_loc_buff);
			f_close(&main_msc.read_file);
			return;
		}


		

		// Handle complex logic for write caching
		if (config.cache_writes == true) {
			// Set the led color to blue to show we are working
			set_led_color(LED_COLOR_BLUE);
			// Open the main cache buffer, this may inflate in file size, and may contain old data
			ret = f_open(&main_msc.cache_file, "/sd/images/cache_buffer.bin", FA_CREATE_ALWAYS | FA_READ | FA_WRITE);
			if (ret != FR_OK) {
				set_led_color(LED_COLOR_RED);
				LOG_E("Failed to open Cache file !\r\n");
				f_close(&main_msc.read_file);
				return;
			}

			// Note that we will be caching writes, and set the number of writes cached so far to 0, as we just started
			main_msc.write_cacheing_length = 0;
			main_msc.write_cacheing = true;

			// Open the cache table file, this will be used to cache writes that are done to disk, in order to make storage and retrival more efficient and faster
			ret = f_open(&main_msc.cache_table_file, "/sd/images/cache_table.bin", FA_CREATE_ALWAYS | FA_READ | FA_WRITE);
			if (ret != FR_OK) {
				set_led_color(LED_COLOR_RED);
				LOG_E("Failed to open Cache table!\r\n");
				f_close(&main_msc.read_file);
				return;
			}
			// Seek to offset 0, just incase it is not done already
			f_lseek(&main_msc.cache_table_file,0);

			UINT fnum;
			// Log how many bytes we must write to generate the cache table
			DWORD cache_file_buff_size = (fsize / main_msc.blocksize) * sizeof(DWORD);
			unsigned int write_buff_size = 4096;

			// Allocate a buffer to be writen to, set it to be filled with 0's. This allows us to write write_buff_size at once
			uint32_t *zero_buffer = malloc(write_buff_size);
			memset(zero_buffer, 0x00, write_buff_size);

			// slightly inneficiently write all bytes required in 4096 block chunks, then what ever remains to be calculated
			int i;
			
			for (i = 0; i< (int)(cache_file_buff_size/write_buff_size) ;i++) {
				ret |= f_write(&main_msc.cache_table_file,zero_buffer,write_buff_size,&fnum);
				if (i % 10) {
					if (type_selected != get_current_mode()) {
						set_led_color(LED_COLOR_RED);
						LOG_I("Cache buffer creation mode switch\r\n");
						free(zero_buffer);
						f_close(&main_msc.read_file);
						f_close(&main_msc.cache_table_file);
						return;
					}
				}
			}
				
			ret |= f_write(&main_msc.cache_table_file,zero_buffer,(cache_file_buff_size - (i*write_buff_size)),&fnum);
			
			// Free the memory allocated as to not cause a leak
			free(zero_buffer);

			if (ret != FR_OK) {
				set_led_color(LED_COLOR_RED);
				LOG_E("Failed to write cache table buffer!\r\n");
				f_close(&main_msc.read_file);
				f_close(&main_msc.cache_table_file);
				return;
			}

			// Change color to green to denote we are done with long operations
			set_led_color(LED_COLOR_GREEN);
			
		}
		
		// Setup the sector_read and write functions
		sector_read = &fs_read_file;
		sector_write = &fs_write_file;
	}

	// Setup USB stuff;
	main_msc.blockcount = fsize / main_msc.blocksize;

	//Init USB
	msc_ram_init();
	usb_started = true;

	return;
}



#if async_fs_writes
void usb_fatfs_finish_operation(void) {
	if (buffer_fatfs_function.operation == 0) return;
	// printf("handling fat request:) %d\r\n",buffer_fatfs_function.operation);

	fatfs_function_call buffer_fatfs_function_backup;

	memcpy((void*) &buffer_fatfs_function_backup, (void*)&buffer_fatfs_function,sizeof(buffer_fatfs_function));
	// here I make a copy of the buffer so I can use it in the function and send a responce without having the problem of sending too quickly
	// the pc wont send a second update untill the last one returned somthing at least.

	buffer_fatfs_function.operation = 0;
	int ret = 0; // 0 = good; 1 = error
	// did you see the thingy
	
	if (buffer_fatfs_function_backup.operation == 1) { // read
		// printf("read senaaaad!\r\n");
		//fail: SCSI_SetSenseData(SCSI_KCQHE_UREINRESERVEDAREA);
		//sucess:  usbd_ep_start_write(mass_ep_data[MSD_IN_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, transfer_len);


		/*
		
		if (usbd_msc_sector_read_return_value == 1) {
        SCSI_SetSenseData(SCSI_KCQHE_UREINRESERVEDAREA);
        return false;
    }

    if (usbd_msc_sector_read_return_value == 0) {
        usbd_ep_start_write(mass_ep_data[MSD_IN_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, transfer_len);
    }

    if (usbd_msc_sector_read_return_value == 2) {
        return 2;
    }

    usbd_msc_cfg.start_sector += (transfer_len / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.nsectors -= (transfer_len / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.csw.dDataResidue -= transfer_len;

    if (usbd_msc_cfg.nsectors == 0) {
        usbd_msc_cfg.stage = MSC_SEND_CSW;
    }
	*/


		buffer_fatfs_function_backup.buffer = malloc(buffer_fatfs_function_backup.length);

		UINT fnum;	


		if (main_msc.write_cacheing == false) {
			f_lseek(&main_msc.read_file, (buffer_fatfs_function_backup.sector*main_msc.blocksize));
			// Read data specified
			ret = f_read(&main_msc.read_file, buffer_fatfs_function_backup.buffer, buffer_fatfs_function_backup.length, &fnum);
		} else {
			// Get the number of blocks needed, based on the length and block size
			uint8_t blocks_to_read = (buffer_fatfs_function_backup.length/main_msc.blocksize);
			// Seek to the offset of the start sector in the cache table
			f_lseek(&main_msc.cache_table_file,buffer_fatfs_function_backup.sector*sizeof(DWORD));
			// Create and store a cache offset table for just this data, based off the cache table
			DWORD cache_offset[blocks_to_read];
			ret |= f_read(&main_msc.cache_table_file,&cache_offset,blocks_to_read*sizeof(DWORD),&fnum);

			// We cant do our operations if there are no bytes to read
			if (blocks_to_read == 0) {
				ret = 1;
				goto read_async_fatfs_main_exit_function;
			}//return 1;
			// Define a variable to store how many previous itterations had blocks in order (could be a uchar)
			int blocks_in_a_row = 0;
			for (int i = 0; i < blocks_to_read; i++) {
				// Skip the first itteration, beacuse we will need to read the previous itteration in the operations to folow
				if (i > 0) {
					// If the previous and current ones are in order, mark as souch, and continue untill this is not the case
					if ((cache_offset[i-1]+1 == cache_offset[i]) || (cache_offset[i-1] == 0 && cache_offset[i] == 0)) { // TODO DETERMIN IF THIS IS STUPID
						blocks_in_a_row += 1;
					} else {
						// Check to see if they were in a row on the cache, or origonal file
						if (cache_offset[i-1] != 0) {
							// Seek to the offset inside the cache file as determined by the first block in the blocks in a row
							f_lseek(&main_msc.cache_file,(cache_offset[i-blocks_in_a_row-1]*main_msc.blocksize));
							// Read off the blocks in a row to their buffers
							ret |= f_read(&main_msc.cache_file, buffer_fatfs_function_backup.buffer + ((i-blocks_in_a_row-1)*main_msc.blocksize), ((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);
						} else {
							// Seek in the origonal file using the offset of the sector where we started reading
							f_lseek(&main_msc.read_file,(buffer_fatfs_function_backup.sector+(i-blocks_in_a_row-1))*main_msc.blocksize);
							// Read off the blocks required
							ret |= f_read(&main_msc.read_file, buffer_fatfs_function_backup.buffer + ((i-blocks_in_a_row-1)*main_msc.blocksize),((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);
						}

						// This block is diffrent, mark as souch
						blocks_in_a_row = 0;
					}
				}
			}

			// Same as inside loop, just dont for reamining blocks
			if (cache_offset[blocks_to_read-1] != 0) {
				f_lseek(&main_msc.cache_file,(cache_offset[blocks_to_read-blocks_in_a_row-1]*main_msc.blocksize));
				ret |= f_read(&main_msc.cache_file, buffer_fatfs_function_backup.buffer + ((blocks_to_read-blocks_in_a_row-1)*main_msc.blocksize), ((blocks_in_a_row + 1)*main_msc.blocksize), &fnum); 
			} else {
				f_lseek(&main_msc.read_file,(buffer_fatfs_function_backup.sector+(blocks_to_read-blocks_in_a_row-1))*main_msc.blocksize);
				ret |= f_read(&main_msc.read_file, buffer_fatfs_function_backup.buffer + ((blocks_to_read-blocks_in_a_row-1)*main_msc.blocksize),((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);
			}

		}
		
		read_async_fatfs_main_exit_function:
		
		if (ret != FR_OK) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Read Ret: %d, fnum: %u\r\n", ret, fnum);
			printf("error");
		}
		// printf("read send!\r\n");
		read_write_async_resp_david(0,1,buffer_fatfs_function_backup.sector,buffer_fatfs_function_backup.buffer,buffer_fatfs_function_backup.length);

		free(buffer_fatfs_function_backup.buffer);

	}
	if (buffer_fatfs_function_backup.operation == 2) { //write
		// printf("writing fat right now bro :)\r\n");


		uint8_t *buffer_fatfs_buffer_clone = malloc(buffer_fatfs_function_backup.length);
		memcpy(buffer_fatfs_buffer_clone,buffer_fatfs_function_backup.buffer,buffer_fatfs_function_backup.length);

		// read_write_async_resp_david(0,2,buffer_fatfs_function_backup.sector,buffer_fatfs_function_backup.buffer,buffer_fatfs_function_backup.length);

		read_write_async_resp_david(0,2,buffer_fatfs_function_backup.sector,buffer_fatfs_buffer_clone, buffer_fatfs_function_backup.length);
		free(buffer_fatfs_buffer_clone);


		// send compleate signal ^^^^




		UINT fnum;

		// do write below 

		if (main_msc.write_cacheing == false) {
			// printf("Non Write cached :)\r\n");
			// seek into the open file, at the location multiplyed by 4096, the sector size (<<9 == 4096)
			f_lseek(&main_msc.read_file, (buffer_fatfs_function_backup.sector*main_msc.blocksize));

			// Write into the file the data with length specified
			ret = f_write(&main_msc.read_file, buffer_fatfs_function_backup.buffer, buffer_fatfs_function_backup.length, &fnum);
			// printf("Write code: %d :)\r\n",ret);
		} else {
			// printf("Write cached :)\r\n");
			// Ok, complex logic, because idfk

			// Get the count of blocks we need to write into the cache
			uint8_t blocks_to_write = (buffer_fatfs_function_backup.length/main_msc.blocksize);

			// Seek into the cache_table_file at the location of the sector we should start at
			f_lseek(&main_msc.cache_table_file,buffer_fatfs_function_backup.sector*sizeof(DWORD));
			// Create two buffers, one cache_offset will be able to be changed, cache_offset_origonal should not be modified, if cache_offset is changed, the disk will be modified
			DWORD cache_offset[blocks_to_write];
			DWORD cache_offset_origonal[blocks_to_write];

			// Read the cache offsets into cache_offset, then copy it to the backup cache_offset_origonal
			ret |= f_read(&main_msc.cache_table_file,&cache_offset,blocks_to_write*sizeof(DWORD),&fnum);
			memcpy(&cache_offset_origonal,&cache_offset,blocks_to_write*sizeof(DWORD));


			// Unneeded, but here anyway, 0 size will legitimatly not work, and previous logic ensures that each read request is % blocksize

			if (blocks_to_write == 0) {
				ret = 1; 
				goto write_async_fatfs_main_exit_function;
			}//return 1;
			

			// Store the number of blocks previously in a row, this is used to optimize disk io operations (could be a uchar)
			int blocks_in_a_row = 0;
			for (int i = 0; i < blocks_to_write; i++) {
				// If the offset is currently in the origonal file, 0, assing it a new index
				if (cache_offset[i] == 0) {
					main_msc.write_cacheing_length += 1;
					cache_offset[i] = main_msc.write_cacheing_length;
				}
				// On every itteration after the first we run this code; we cant run on first because we have to compare the previous itteration
				if (i > 0) {
					// Check if we are going to write in a row, if so, add it to the count
					if (cache_offset[i-1]+1 == cache_offset[i]) {
						blocks_in_a_row += 1;
					} else {
						// If not, seek into the cache file at the start of the blocks that are in a row
						f_lseek(&main_msc.cache_file,(cache_offset[i-blocks_in_a_row-1]*main_msc.blocksize));
						// Write the data from the previous, in a row, blocks, instead of multiple disk io operations
						ret |= f_write(&main_msc.cache_file, buffer_fatfs_function_backup.buffer + ((i-blocks_in_a_row-1)*main_msc.blocksize), ((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);
				
						// We are not the same as previous, we need to reset this 
						blocks_in_a_row = 0;
					}
				}
			}

			// If cache_offset got changed, mark that it changed (this can be optimized to a boolean)
			if (memcmp(&cache_offset_origonal,&cache_offset,blocks_to_write*sizeof(DWORD)) != 0) {
				// Seek again into the correct offset for the cache table
				f_lseek(&main_msc.cache_table_file,buffer_fatfs_function_backup.sector*sizeof(DWORD));
				// Write the exact same way we read from it
				ret |= f_write(&main_msc.cache_table_file,&cache_offset,blocks_to_write*sizeof(DWORD),&fnum);
			}
			// Re execute code for similar / blocks in a row, because the previous block needs to be writen for us to be able to get all them writen
			f_lseek(&main_msc.cache_file,(cache_offset[blocks_to_write-blocks_in_a_row-1]*main_msc.blocksize));
						
			ret |= f_write(&main_msc.cache_file, buffer_fatfs_function_backup.buffer + ((blocks_to_write-blocks_in_a_row-1)*main_msc.blocksize), ((blocks_in_a_row + 1)*main_msc.blocksize), &fnum);

		}


		write_async_fatfs_main_exit_function:
		// If any errors occored, exit and warn
		if (ret) {
			set_led_color(LED_COLOR_RED);
			LOG_E("Write Ret: %d, fnum: %u\r\n", ret, fnum);
			// printf("error");
		}




	/*
	if (usbd_msc_sector_write_return_value == 1) {
        SCSI_SetSenseData(SCSI_KCQHE_WRITEFAULT);
        return false;
    }

    if (usbd_msc_sector_write_return_value == 2) {
        return usbd_msc_sector_write_return_value;
    }

    usbd_msc_cfg.start_sector += (nbytes / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.nsectors -= (nbytes / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.csw.dDataResidue -= nbytes;

    if (usbd_msc_cfg.nsectors == 0) {
        // return true;
        usbd_msc_send_csw(CSW_STATUS_CMD_PASSED);
    } else {
        data_len = MIN(usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size, CONFIG_USBDEV_MSC_BLOCK_SIZE);
        usbd_ep_start_read(mass_ep_data[MSD_OUT_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, data_len);
    }

	*/

		
		free(buffer_fatfs_function_backup.buffer);
	}

	
	if (ret != 0) {
		set_led_color(LED_COLOR_RED);
		LOG_E("Fatfs delayed operation failed: %d\r\n");
		// printf("error");
		// TODO: REPLACE THIS
		run_usbdeinit();
	}




	/*
	line 1028 

	if (usbd_msc_cfg.stage == MSC_READ_CBW) {
            if (len2send) {
                USB_LOG_DBG("Send info len:%d\r\n", len2send);
                usbd_msc_send_info(buf2send, len2send);
            } else {
                usbd_msc_send_csw(CSW_STATUS_CMD_PASSED);
            }
        }



	*/

	
}
void setup_fatfs_operation_cache(void) {

}
#endif


void ATTR_TCM_SECTION reflash_self_from_sd() {
	// we will do fatfs read and size in this fuction, no need for that

	// Oh ok then make th
	// bflb_flash.h
	// just because address is used soo much, I put it in there, I should do a 

	uint8_t *psram_buf = (uint8_t *)BFLB_PSRAM_BASE;


	set_led_color(LED_COLOR_BLUE);
	int ret = 0;
	uint64_t fsize = 0;
	// Try to mount the sd card
	ret = f_mount(&fs, "/sd", 1);
	if (ret != FR_OK) {
		printf("Sd mount failed reflash\r\n");
		set_led_color(LED_COLOR_RED);
		while(1);
	}


	FIL read_image;
	ret = f_open(&read_image, "/sd/images/flash_image.bin", FA_READ);
	if (ret != FR_OK) {
		printf("No sd image\r\n");
		set_led_color(LED_COLOR_RED);
		while(1);
	}


	fsize = f_size(&read_image);

	

	f_lseek(&read_image,0);



	// uint32_t i=0;
	UINT fnum;
	

	// for (i=0; i<=(fsize-4096); i+=4096) {
	// 	ret |= f_read(&read_image,(uint8_t *)(BFLB_PSRAM_BASE+i),4096,&fnum);
	// }
	// if ((fsize%4096) != 0) {
	// 	ret |= f_read(&read_image,(uint8_t *)(BFLB_PSRAM_BASE+i+(fsize%4096)),(fsize%4096),&fnum);
	// }

	ret = f_read(&read_image, (void *)BFLB_PSRAM_BASE, fsize, &fnum);

	ret |= f_close(&read_image);

	if (ret != 0) {
		printf("failed to read\r\n");
		set_led_color(LED_COLOR_RED);
		while(1);
	}
	
	
	f_unmount("/sd");


	extern struct bflb_device_s *console;

	printf("Peace out; cant do anything else :|\r\n");


	uint8_t *rw_test_buffer = malloc(4096);

	//bl808_l1c.h
	L1C_DCache_Clean_Invalid_All();
	L1C_DCache_Clean_All();
	L1C_DCache_Disable();
	//bl808_l1c.h
	bflb_irq_save();

	bflb_uart_putchar(console, 'S');
	

	//bflb_flash_init
	

	uint32_t temp = 0;
	uint32_t flash_offset = 0x0;

	// for (unsigned int j=0; j<fsize; j+=4096) {
	// 	temp = ((fsize-j)>4096)?4096:(fsize-j);
	// 	bflb_flash_erase(flash_offset+j, temp);
	// 	bflb_uart_putchar(console, 'E');
	// }
	bflb_flash_erase(flash_offset, fsize);
	bflb_uart_putchar(console, 'E');

	for (unsigned int j=0; j<fsize; j+=4096) {
		temp = ((fsize-j)>4096)?4096:(fsize-j);
		bflb_flash_read(flash_offset+j,rw_test_buffer, temp);
		
		for (unsigned int k=0; k<temp; k++) {
			if (rw_test_buffer[k] != 0xFF) {
				bflb_uart_putchar(console, 'R');
			}
		}
	}
	
	
	// for (unsigned int j=0; j<fsize; j+=4096) {
	// 	temp = ((fsize-j)>4096)?4096:(fsize-j);
	// 	bflb_flash_write(flash_offset+j, &psram_buf[j], temp);
	// 	bflb_uart_putchar(console, 'F');
	// }
	bflb_flash_write(flash_offset, &psram_buf[0], fsize);
	bflb_uart_putchar(console, 'F');

	for (unsigned int j=0; j<fsize; j+=4096) {
		temp = ((fsize-j)>4096)?4096:(fsize-j);
		bflb_flash_read(flash_offset+j,rw_test_buffer, temp);
		
		for (unsigned int k=0; k<temp; k++) {
			if (rw_test_buffer[k] != psram_buf[j+k]) {
				bflb_uart_putchar(console, 'T');
			}
		}
	}
	// bflb_flash_write(0x0, &psram_buf[0], fsize);

	// __flash_do_actually_flash(0x0, &psram_buf[0], 0x1000);
	// __flash_do_actually_flash(0x0, &psram_buf[0], 0x1000);


	// for (i=0; i<=(fsize-4096); i+=4096) {
	// 	// f_read(&read_image,(uint8_t *)(BFLB_PSRAM_BASE+i),4096,&fnum);
	// 	bflb_flash_write(i,(uint8_t *)(BFLB_PSRAM_BASE+i),4096);

	// }
	// if ((fsize%4096) != 0) {
	// 	// f_read(&read_image,(uint8_t *)(BFLB_PSRAM_BASE+i+(fsize%4096)),(fsize%4096),&fnum);

	// 	bflb_flash_write(i,(uint8_t *)(BFLB_PSRAM_BASE+i+(fsize%4096)),(fsize%4096));
	// }

	// uint8_t pin = GPIO_PIN_21;
	// putreg32(1 << (pin & 0x1f), gpio->reg_base + (0xAF4) + ((pin >> 5) << 2));
	// RAW gpio set
	bflb_uart_putchar(console, 'D');

	reboot_chip(FLASH_ROM_OFFSET_M0);

	// uint8_t pin = GPIO_PIN_23;
	// putreg32(1 << (pin & 0x1f), gpio->reg_base + (0xAF4) + ((pin >> 5) << 2));





	

	// int interupt_sate = bflb_irq_save(); //Wait read the file and get its size, then do this

	// get file; read first block; erase full size; write first block, repeat 
	// is there any way to load those into memory? do we need to copy the full image size?
	// do we have to memory for that
	//We can use Psram, 64MB
	// ok bro sec
	//Nope. Erase full will erase sd reading/writing functions
	

	//bflb_irq.h


	
	// I should prob use macros or a function, but this works too, what do you think?
	// Yeah, and if you want you can define a macro for a function pointer, but thats complicated
	// ok, so change to a func? I mean thats complicated. Leave it for now,
	// I mean


	// did you wana flash to psram, or just use as temp storage before copying to here?
	
	// REBOOT_CHIP

	// there no more errors :)


	// how should I actualy call this btw??
	// this errors
	//Can you s

	// ^^^^^^^ expression preceding parentheses of apparent call must have (pointer-to-) function typeC/C++(109)

}
//ATTR_TCM_SECTION makes the function run from ram, not flash


#if flash_mode

void ATTR_TCM_SECTION finish_flash_function(void) {

	uint8_t *psram_buf = (uint8_t *)BFLB_PSRAM_BASE;

	set_led_color(LED_COLOR_BLUE);


	if (!flash_correct_board) {
		LOG_E("Wrong board");
		set_led_color(LED_COLOR_RED);
	};
	
	// TIMEOUT FOR 1 second; let the pc finish its write, done be too fast

	bflb_mtimer_delay_ms(1000);
	run_usbdeinit();

	

	f_close(&main_msc.read_file);
	f_close(&main_msc.cache_file);
	f_close(&main_msc.cache_table_file);

	f_unmount("/sd");


	printf("Flashing start, rest will be single characters\r\n");


	uint8_t *rw_test_buffer = malloc(4096);

	extern struct bflb_device_s *console;

	//bl808_l1c.h
	L1C_DCache_Clean_Invalid_All();
	L1C_DCache_Clean_All();
	L1C_DCache_Disable();
	//bl808_l1c.h
	bflb_irq_save();

	bflb_uart_putchar(console, 'S');
	

	//bflb_flash_init
	

	uint32_t temp = 0;
	uint32_t flash_offset = 0x0;

	bflb_flash_erase(flash_offset, flash_total_length);
	bflb_uart_putchar(console, 'E');

	for (unsigned int j=0; j<flash_total_length; j+=4096) {
		temp = ((flash_total_length-j)>4096)?4096:(flash_total_length-j);
		bflb_flash_read(flash_offset+j,rw_test_buffer, temp);
		
		for (unsigned int k=0; k<temp; k++) {
			if (rw_test_buffer[k] != 0xFF) {
				bflb_uart_putchar(console, 'R');
			}
		}
	}
	
	

	bflb_flash_write(flash_offset, &psram_buf[0], flash_total_length);
	bflb_uart_putchar(console, 'F');

	for (unsigned int j=0; j<flash_total_length; j+=4096) {
		temp = ((flash_total_length-j)>4096)?4096:(flash_total_length-j);
		bflb_flash_read(flash_offset+j,rw_test_buffer, temp);
		
		for (unsigned int k=0; k<temp; k++) {
			if (rw_test_buffer[k] != psram_buf[j+k]) {
				bflb_uart_putchar(console, 'T');
			}
		}
	}

	bflb_uart_putchar(console, 'D');

	reboot_chip(FLASH_ROM_OFFSET_M0);
}


#endif


/**
 * @brief Main function that is executed as one of the first things
 * 
 */
int main(void)
{
	// Start bouffalo sdk things like printf
	board_init();

	// Start gpio reads and writes
	board_sdh_gpio_init();
	// Allow use of gpio device, storing refrence in gpio
	gpio = bflb_device_get_by_name("gpio");
	
	// Setup gpio inputs and outputs
	led_init();
	mode_select_init();

	// Init the SDCARD
	fatfs_sdh_driver_register();

	// Get current mode so we can tell when it changes
	int type_selected = get_current_mode();
	int new_selected = type_selected;

	
	if (type_selected == 15 && (!bflb_gpio_read(gpio, GPIO_PIN_28)) ) { // if in mode 15 and button is pressed :)
		set_led_color(LED_COLOR_GREEN);
		bflb_mtimer_delay_ms(1000);
		for (int i=0;i<20;i++) {
			set_led_color(LED_COLOR_GREEN);
			bflb_mtimer_delay_ms(200);
			set_led_color(LED_COLOR_RED);
			bflb_mtimer_delay_ms(200);
			set_led_color(LED_COLOR_BLUE);
			bflb_mtimer_delay_ms(200);
		}

		reflash_self_from_sd();
	} 


	main_msc.blockcount = 0;
	main_msc.blocksize = 0;
	main_msc.write_cacheing = false;
	main_msc.ro = false;
	main_msc.write_cacheing_length = 0;


	#if async_fs_writes
		setup_fatfs_operation_cache();
	#endif

	while (1) {
		// Call main function for setup
		run_usbinit(type_selected);

		
		
		// Wait for mode to change
		while (new_selected == type_selected) {
			new_selected = get_current_mode();
			#if async_fs_writes
				usb_fatfs_finish_operation();
			#endif

			#if flash_mode
				if (flash_done) {
					finish_flash_function();
				}
			#endif
			// bflb_mtimer_delay_ms(50); //average time is a little over 25 ms to change, but this is fine
		}

#if async_writes
		SDH_WriteMultiBlocks_async_finish();
		// we dont care about this failing
#endif



		type_selected = new_selected;
		
		// Stop all usb handling as soon as we change, as to not cause issues
		run_usbdeinit();

		// Addin just incase, we dont care about return value at all, even if object is null, we just want it gone
		f_close(&main_msc.read_file);
		f_close(&main_msc.cache_file);
		f_close(&main_msc.cache_table_file);

		main_msc.ro = false;
		main_msc.write_cacheing = false;

		//debounce :)
		bflb_mtimer_delay_ms(200);
		// not easy to notice, but helps a LOT

		// fix incase was changed durring debounce :)
		// new_selected = get_current_mode();
		// type_selected = new_selected;

	}
}
