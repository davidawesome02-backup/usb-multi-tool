// Define imports from usb librarys
#include "usbd_core.h"
#include "usbd_hid.h"
#include "bflb_l1c.h"



#define USBD_VID           0xffff
#define USBD_PID           0xffff
// in 5 mill amp parts, so 5v 
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033


//Keyboard
// Random number for the endpoint
#define HID_KEYB_INT_EP           0x82
// packet return size
#define HID_KEYB_INT_EP_SIZE      8
// interval in ms
#define HID_KEYB_INT_EP_INTERVAL  1
#define HID_KEYB_REPORT_DESC_SIZE 63

//Mouse 
// Random number for the endpoint
#define HID_MOUS_INT_EP           0x83
// Packet return size
#define HID_MOUS_INT_EP_SIZE      4
// interval in ms
#define HID_MOUS_INT_EP_INTERVAL  1
#define HID_MOUS_REPORT_DESC_SIZE 52


#define USB_HID_CONFIG_DESC_SIZ       59

#define HID_STATE_IDLE 0
#define HID_STATE_BUSY 1

// Max keys we can send at once
#define MAXKEYS 	6

/*!< mouse report struct */
struct hid_mouse {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
};

/*!< hid state ! Data can be sent only when state is idle  */
static volatile uint8_t keyb_hid_state = HID_STATE_BUSY;
static volatile uint8_t mous_hid_state = HID_STATE_BUSY;

static volatile uint8_t hid_connected = 0;
// var to define how many delta times init was ran vs deinit.
int number_of_times_user_has_started = 0;

// Combined hid describtor showing mouse and keyboard
static const uint8_t hid_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0002, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_HID_CONFIG_DESC_SIZ, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER), //Two interfaces

    /* Keyboard */
    /************** Descriptor of Joystick Mouse interface ****************/
    /* 09 */
    0x09,                          /* bLength: Interface Descriptor size */
    USB_DESCRIPTOR_TYPE_INTERFACE, /* bDescriptorType: Interface descriptor type */
    0x00,                          /* bInterfaceNumber: Number of Interface */
    0x00,                          /* bAlternateSetting: Alternate setting */
    0x01,                          /* bNumEndpoints */
    0x03,                          /* bInterfaceClass: HID */
    0x01,  /* 2 == mouse */                        /* bInterfaceSubClass : 1=BOOT, 0=no boot */
    0x01,                          /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
    0,                             /* iInterface: Index of string descriptor */
    /******************** Descriptor of Joystick Mouse HID ********************/
    /* 18 */
    0x09,                    /* bLength: HID Descriptor size */
    HID_DESCRIPTOR_TYPE_HID, /* bDescriptorType: HID */
    0x11,                    /* bcdHID: HID Class Spec release number */
    0x01,
    0x00,                          /* bCountryCode: Hardware target country */
    0x01,                          /* bNumDescriptors: Number of HID class descriptors to follow */
    0x22,                          /* bDescriptorType */
	HID_KEYB_REPORT_DESC_SIZE,     /* wItemLength: Total length of Report descriptor */
    0x00,
    /******************** Descriptor of Keyboard endpoint ********************/
    /* 27 */
    0x07,                         /* bLength: Endpoint Descriptor size */
    USB_DESCRIPTOR_TYPE_ENDPOINT, /* bDescriptorType: */
    HID_KEYB_INT_EP,              /* bEndpointAddress: Endpoint Address (IN) */
    0x03,                         /* bmAttributes: Interrupt endpoint */
    HID_KEYB_INT_EP_SIZE,         /* wMaxPacketSize: 8 Byte max */
    0x00,
    HID_KEYB_INT_EP_INTERVAL,     /* bInterval: Polling Interval */
    /* 34 */



    /* Mouse */
    /************** Descriptor of Joystick Mouse interface ****************/
    /* 34 */
    0x09,                          /* bLength: Interface Descriptor size */
    USB_DESCRIPTOR_TYPE_INTERFACE, /* bDescriptorType: Interface descriptor type */
    0x01,                          /* bInterfaceNumber: Number of Interface */
    0x00,                          /* bAlternateSetting: Alternate setting */
    0x01,                          /* bNumEndpoints */
    0x03,                          /* bInterfaceClass: HID */
    0x01,                          /* bInterfaceSubClass : 1=BOOT, 0=no boot */
    0x02,                          /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
    0,                             /* iInterface: Index of string descriptor */
    /******************** Descriptor of Joystick Mouse HID ********************/
    /* 43 */
    0x09,                    /* bLength: HID Descriptor size */
    HID_DESCRIPTOR_TYPE_HID, /* bDescriptorType: HID */
    0x11,                    /* bcdHID: HID Class Spec release number */
    0x01,
    0x00,                          /* bCountryCode: Hardware target country */
    0x01,                          /* bNumDescriptors: Number of HID class descriptors to follow */
    0x22,                          /* bDescriptorType */
	HID_MOUS_REPORT_DESC_SIZE,     /* wItemLength: Total length of Report descriptor */
    0x00,
    /******************** Descriptor of Mouse endpoint ********************/
    /* 52 */
    0x07,                         /* bLength: Endpoint Descriptor size */
    USB_DESCRIPTOR_TYPE_ENDPOINT, /* bDescriptorType: */
    HID_MOUS_INT_EP,              /* bEndpointAddress: Endpoint Address (IN) */
    0x03,                         /* bmAttributes: Interrupt endpoint */
    HID_MOUS_INT_EP_SIZE,         /* wMaxPacketSize: 8 Byte max */
    0x00,
    HID_MOUS_INT_EP_INTERVAL,     /* bInterval: Polling Interval */
    /* 59 */

    ///////////////////////////////////////
    /// string0 descriptor
    ///////////////////////////////////////
    USB_LANGID_INIT(USBD_LANGID_STRING),
    ///////////////////////////////////////
    /// string1 descriptor
    ///////////////////////////////////////
    0x14,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'C', 0x00,                  /* wcChar0 */
    'h', 0x00,                  /* wcChar1 */
    'e', 0x00,                  /* wcChar2 */
    'r', 0x00,                  /* wcChar3 */
    'r', 0x00,                  /* wcChar4 */
    'y', 0x00,                  /* wcChar5 */
    'U', 0x00,                  /* wcChar6 */
    'S', 0x00,                  /* wcChar7 */
    'B', 0x00,                  /* wcChar8 */
    ///////////////////////////////////////
    /// string2 descriptor
    ///////////////////////////////////////
    0x26,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'C', 0x00,                  /* wcChar0 */
    'h', 0x00,                  /* wcChar1 */
    'e', 0x00,                  /* wcChar2 */
    'r', 0x00,                  /* wcChar3 */
    'r', 0x00,                  /* wcChar4 */
    'y', 0x00,                  /* wcChar5 */
    'U', 0x00,                  /* wcChar6 */
    'S', 0x00,                  /* wcChar7 */
    'B', 0x00,                  /* wcChar8 */
    ' ', 0x00,                  /* wcChar9 */
    'H', 0x00,                  /* wcChar10 */
    'I', 0x00,                  /* wcChar11 */
    'D', 0x00,                  /* wcChar12 */
    ' ', 0x00,                  /* wcChar13 */
    'D', 0x00,                  /* wcChar14 */
    'E', 0x00,                  /* wcChar15 */
    'M', 0x00,                  /* wcChar16 */
    'O', 0x00,                  /* wcChar17 */
    ///////////////////////////////////////
    /// string3 descriptor
    ///////////////////////////////////////
    0x16,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    '2', 0x00,                  /* wcChar0 */
    '0', 0x00,                  /* wcChar1 */
    '2', 0x00,                  /* wcChar2 */
    '2', 0x00,                  /* wcChar3 */
    '1', 0x00,                  /* wcChar4 */
    '2', 0x00,                  /* wcChar5 */
    '3', 0x00,                  /* wcChar6 */
    '4', 0x00,                  /* wcChar7 */
    '5', 0x00,                  /* wcChar8 */
    '6', 0x00,                  /* wcChar9 */
#ifdef CONFIG_USB_HS
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x01,
    0x00,
#endif
    0x00
};

// Report usb description of keyboard endpoint
static const uint8_t hid_keyboard_report_desc[HID_KEYB_REPORT_DESC_SIZE] = {
0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
0x09, 0x06,        // Usage (Keyboard)
0xA1, 0x01,        // Collection (Application)
//0x85, 0x02,        //   Report ID (2)
0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
0x19, 0xE0,        //   Usage Minimum (0xE0)
0x29, 0xE7,        //   Usage Maximum (0xE7)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x75, 0x01,        //   Report Size (1)
0x95, 0x08,        //   Report Count (8)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x95, 0x01,        //   Report Count (1)
0x75, 0x08,        //   Report Size (8)
0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x95, 0x05,        //   Report Count (5)
0x75, 0x01,        //   Report Size (1)
0x05, 0x08,        //   Usage Page (LEDs)
0x19, 0x01,        //   Usage Minimum (Num Lock)
0x29, 0x05,        //   Usage Maximum (Kana)
0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x95, 0x01,        //   Report Count (1)
0x75, 0x03,        //   Report Size (3)
0x91, 0x03,        //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x95, 0x06,        //   Report Count (6)
//0x95, 0x05,        //   Report Count (5)
0x75, 0x08,        //   Report Size (8)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0xFF,        //   Logical Maximum (-1)
0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
0x19, 0x00,        //   Usage Minimum (0x00)
0x29, 0x65,        //   Usage Maximum (0x65)
0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              // End Collection
};
// Report usb description of mouse endpoint
static const uint8_t hid_mouse_report_desc[HID_MOUS_REPORT_DESC_SIZE] = {
0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
0x09, 0x02,        // Usage (Mouse)
0xA1, 0x01,        // Collection (Application)
//0x85, 0x01,        //   Report ID (1)
0x09, 0x01,        //   Usage (Pointer)
0xA1, 0x00,        //   Collection (Physical)
0x05, 0x09,        //     Usage Page (Button)
0x19, 0x01,        //     Usage Minimum (0x01)
0x29, 0x03,        //     Usage Maximum (0x03)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x95, 0x03,        //     Report Count (3)
0x75, 0x01,        //     Report Size (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x95, 0x01,        //     Report Count (1)
0x75, 0x05,        //     Report Size (5)
0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x09, 0x31,        //     Usage (Y)
0x09, 0x38,        //     Usage (Wheel)
0x15, 0x81,        //     Logical Minimum (-127)
0x25, 0x7F,        //     Logical Maximum (127)
0x75, 0x08,        //     Report Size (8)
0x95, 0x03,        //     Report Count (3)
0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0xC0,              // End Collection*/

// 117 bytes
};




// Define use of get_current_mode from main (used later on to return on change)
extern unsigned char get_current_mode();

/**
 * @brief Convert ascii characters to hid symbols
 * @param input singe ascii char to convert
 */
uint8_t ascii_to_hid(char input) {
	if ('a'<=input && input<='z') input-='a'-'A'; //tolower
	if ('A'<=input && input<='Z') return (input-'A')+4;
	if ('1'<=input && input<='9') return(input-='1')+0x1e;
	
	switch(input) {
      case '!':   return(0x1E);
      case '@':   return(0x1F);
      case '#':   return(0x20);
      case '$':   return(0x21);
      case '%':   return(0x22);
      case '^':   return(0x23);
      case '&':   return(0x24);
      case '*':   return(0x25);
      case '(':   return(0x26);
      case ')':   return(0x27);
      case '0':   return(0x27);
      case '\t':  return(0x2B);  //tab
      case ' ':   return(0x2C);  //space
      case '_':   return(0x2D);
      case '-':   return(0x2D);
      case '+':   return(0x2E);
      case '=':   return(0x2E);
      case '{':   return(0x2F);
      case '[':   return(0x2F);
      case '}':   return(0x30);
      case ']':   return(0x30);
      case '|':   return(0x31);
      case '\\':   return(0x31);
      case ':':   return(0x33);
      case ';':   return(0x33);
      case '"':   return(0x34);
      case '\'':   return(0x34);
      case '~':   return(0x35);
      case '`':   return(0x35);
      case '<':   return(0x36);
      case ',':   return(0x36);
      case '>':   return(0x37);
      case '.':   return(0x37);
      case '?':   return(0x38);
      case '/':   return(0x38);
      case '\n':   return(0x28);

   }
   return 0;
}


/**
 * @brief Convert ascii chars to caps needed to properly send the hid symbol
 * @param input singe ascii char to convert
 */
bool ascii_to_caps(char input) { //bitshift then xor with user settings to get what to send
	if ('A'<=input && input<='Z') return true;
	switch (input) {
		case '!': return true;
		case '@': return true;
		case '#': return true;
		case '$': return true;
		case '%': return true;
		case '^': return true;
		case '&': return true;
		case '*': return true;
		case '(': return true;
		case ')': return true;
		case '{': return true;
		case '}': return true;
		case '_': return true;
		case '+': return true;
		case '|': return true;
		case '"': return true;
		case ':': return true;
		case '<': return true;
		case '>': return true;
		case '?': return true;
		case '~': return true;
	}
	return false;
}


/**
 * @brief Sends a set of at max 6 keys over usb
 * @param characters set of hid characters in uint8_t format
 * @param char_len length (max 6) of characters supplyed in characters
 * @param modifiers modifiers keys, like alt or shift or control
 * @param resend_count number of times to resend this keypress
 * @param type_selected Current type selected, will exit if this gets changed
 */
void sendkey(uint8_t *characters,unsigned char char_len,uint8_t modifiers, int resend_count, int type_selected) {
    // Loop how many times we need to resend
	for (int p=0;p<resend_count;p=p) {
		while (keyb_hid_state == HID_STATE_BUSY && (get_current_mode() == type_selected)) {} //put before so it can run all processing requirements before waiting (speed optimization)
        // If the mode changed, exit rq
        if ((get_current_mode() != type_selected)) return;
        
        // Make a base buffer to modify
		uint8_t sendbuffer[8] = {
			modifiers, 	//Modifiers
			0x00,		//reserved
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00 	//Keys
		};

        // Copy the keys into the sendbuffer
		memcpy(&sendbuffer[2],characters,sizeof(uint8_t)*char_len);

        // Dont cache this memory, get ready to send
	    bflb_l1c_dcache_clean_range(sendbuffer, 8);
        // Send the memory over usb as keyboard
	    int ret = usbd_ep_start_write(HID_KEYB_INT_EP, sendbuffer, 8);
        // If we error, repeat the send
	    if (ret < 0) {
			printf("err2 %d\r\n",ret); 
			continue;
	    }
        // Only increment if it succeded
	    p+=1;
        // Set that we are waiting for usb to request more data
	    keyb_hid_state = HID_STATE_BUSY;
	}
}

void usbd_configure_done_callback(void)
{
    // Just connected, just say we are connected and ready to send data
    keyb_hid_state = HID_STATE_IDLE;
    mous_hid_state = HID_STATE_IDLE;
    hid_connected = 1;
    
}

/* Keyboard interupt recived*/
void usbd_keyb_hid_int_callback(uint8_t ep, uint32_t nbytes)
{
    // Say we are ready to send more data
	keyb_hid_state = HID_STATE_IDLE;
}
// Define a endpoint for use later
static struct usbd_endpoint hid_keyb_in_ep = {
    .ep_cb = usbd_keyb_hid_int_callback,
    .ep_addr = HID_KEYB_INT_EP
};

/* Mouse interupt recived*/
void usbd_mous_hid_int_callback(uint8_t ep, uint32_t nbytes)
{
    // Say we are ready to send more data
	mous_hid_state = HID_STATE_IDLE;
}

// Define a endpoint for use later
static struct usbd_endpoint hid_mous_in_ep = {
    .ep_cb = usbd_mous_hid_int_callback,
    .ep_addr = HID_MOUS_INT_EP
};


struct usbd_interface intf1;
struct usbd_interface intf2;

/**
 * @brief Disconnects usb device
 */
void hid_disconnect(void) {
    // Check if we can reset (if it was started)
    if (number_of_times_user_has_started > 0) {
        usbd_event_reset_handler();
        usbd_deinitialize();
        hid_connected = 0;

        // Set that the user cannot stop the program now, as it is stopped
        number_of_times_user_has_started = 0;
    }
}

/**
 * @brief Allows usb device to be connected to
 */
void hid_init(void)
{
    // Send base descriptors
    usbd_desc_register(hid_descriptor);

    // Send both interfaces avalable
    usbd_add_interface(usbd_hid_init_intf(&intf1, hid_keyboard_report_desc, HID_KEYB_REPORT_DESC_SIZE));
    usbd_add_interface(usbd_hid_init_intf(&intf2, hid_mouse_report_desc, HID_MOUS_REPORT_DESC_SIZE));

    // Add our callbacks on message request
    usbd_add_endpoint(&hid_keyb_in_ep);
    usbd_add_endpoint(&hid_mous_in_ep);
    
    // Start reciving messages
    usbd_initialize();

    // Say we are not currently connected on usb, only ready for it
    hid_connected = 0;

    //User started; add one to the counter
    number_of_times_user_has_started += 1;

}

/**
 * @brief Send a mouse struct over usb
 * @param mouse mouse object to send
 * @param type_selected the current mode the user is in, this method will return when changed
 */
void sendmouse(struct hid_mouse mouse,int type_selected)
{
    // Make a blank buffer
	uint8_t sendbuffer[8] = {0};

    // Wait for mouse to be ready for data or user mode to change
	while (mous_hid_state == HID_STATE_BUSY  && (get_current_mode() == type_selected)) {}

    // if user mode changed, just return out
    if ((get_current_mode() != type_selected)) return;


    /*!< move mouse pointer */
    // loop untill we suceed at sending the packet
    while (get_current_mode() == type_selected) { 
        // Prepare mouse packet
		memcpy(&sendbuffer[0], &mouse ,sizeof(mouse));
        // Make memory ready 
		bflb_l1c_dcache_clean_range(sendbuffer, 8);

        // send packet
		int ret = usbd_ep_start_write(HID_MOUS_INT_EP, sendbuffer, HID_MOUS_INT_EP_SIZE);
	    if (ret < 0) {
	    	printf("err1 %d\r\n",ret);
	        continue;
	    }
	    mous_hid_state = HID_STATE_BUSY;
	    break;
    }
}

/**
 * @brief Waits at max 100 ms to return value (returns true if usb connected, false if we need to wait longer)
 *
 */
bool wait_for_usb_connection(void) {
    uint32_t time = 100;
    uint64_t start_time = bflb_mtimer_get_time_ms();
	while (hid_connected != 1 && (bflb_mtimer_get_time_ms() - start_time < time));
    return (bflb_mtimer_get_time_ms() - start_time < time);
}



/**
 * @brief Sends keys in the most optimal way from a string
 * @param data String of data to send
 * @param modifiers The shift status or other statuses (will be xored with status required to print a character)
 * @param type_selected Will return when user mode changes from this value
 */
void send_str(char data[],uint8_t modifiers,int resend_count, int type_selected) {
    // The length of the data we recived when the function was ran
	int _length_of_array = strlen(data);
    // The number of characters in the last send buffer
	int charcount_last_send=0;
    // Array containing the previous send characters from this function
	uint8_t chars_last_send[MAXKEYS]={0};
    // The length of the chars_ready_array should be same as here
	int charcount_ready=0;
    // Character array containing chars that all have same shift status, with a length of 6
	uint8_t chars_ready[MAXKEYS]={0};
    // Stores the shift state if the current buffer, 0 = off, 1 = on, 2 = dont care, previous was just sent 
	unsigned char shift_status = 2;
    // Temp Store the current characters shift status, so we dont have to rerun
	bool current_shift_status_check = 0;
    // Temp storage for swaping ascii to hid, just so we dont have to rerun
	uint8_t temp_hid_code_storage=0;
    // Loop over every character in the length 
	for(int i=0;i<_length_of_array;i++) {
        // Store the current shift status of the character we typed
		current_shift_status_check = ascii_to_caps(data[i]);
        // If the previous status was the same or didnt care, save it and we are fine
		if (shift_status == 2 || shift_status == current_shift_status_check) {
			shift_status = current_shift_status_check;
		} else {
            // If the shift status differs, send the current keys, and swap the buffers, then set the shift status
			sendkey(chars_ready,charcount_ready,(shift_status<<1)^modifiers,resend_count,type_selected);
			charcount_last_send = charcount_ready;
			memcpy(&chars_last_send,&chars_ready,charcount_ready);
			charcount_ready=0;	
			shift_status = current_shift_status_check;
		}

        // Translate the current character to hid codes
		temp_hid_code_storage = ascii_to_hid(data[i]);

        // Check to make sure the current char is not in the characters we just sent, if it is, send the current buffer
		for (int o=0;o<charcount_ready;o++) {
			if (chars_ready[o] == temp_hid_code_storage) {
				sendkey(chars_ready,charcount_ready,(shift_status<<1)^modifiers,resend_count,type_selected);
				charcount_last_send = charcount_ready;
				memcpy(&chars_last_send,&chars_ready,charcount_ready);
				charcount_ready=0;	
			}
		}
        // If previous one triggered, this one will too, but it checks the last sent chars to see if they contained the current character, and if so, send the current buffer
		for (int o=0;o<charcount_last_send;o++) {
			if (chars_last_send[o] == temp_hid_code_storage) {
				sendkey(chars_ready,charcount_ready,(shift_status<<1)^modifiers,resend_count,type_selected);
				charcount_last_send = charcount_ready;
				memcpy(&chars_last_send,&chars_ready,charcount_ready);
				charcount_ready=0;	
			}
		}
		
		
        // Append out character to the array of ready characters
		chars_ready[charcount_ready] = temp_hid_code_storage;
		// Increse the number of characters that are ready to send
        charcount_ready += 1;
        
        // If we have a full buffer, send the keys, and ignore the next shift value
		if (charcount_ready==MAXKEYS) {
			sendkey(chars_ready,charcount_ready,(shift_status<<1)^modifiers,resend_count,type_selected);
			charcount_last_send = charcount_ready;
			memcpy(&chars_last_send,&chars_ready,charcount_ready);

			charcount_ready=0;	
			shift_status = 2;
		}
	}
    // Send left over buffer incase we wernt exact enough
	sendkey(chars_ready,charcount_ready,(shift_status<<1)^modifiers,resend_count,type_selected);
    // Send key up for all
	sendkey(chars_ready,0,0,resend_count,type_selected); 

}









