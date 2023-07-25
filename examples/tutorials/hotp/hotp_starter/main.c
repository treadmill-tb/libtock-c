// USB Security Key Application
//
// Outputs HOTP codes over USB HID
//
// Test with: https://www.verifyr.com/en/otp/check#hotp
// Use the "Generate HOTP Code" window with the default secret "test" or whatever secret you use
// Counter should be the current counter value
// MUST use algorithm "sha256"
// Digits should be "6" unless you later change that

// C standard library includes
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Libtock includes
#include <app_state.h>
#include <button.h>
#include <console.h>
#include <hmac.h>
#include <led.h>
#include <timer.h>
#include <usb_keyboard_hid.h>

// Local includes
#include "base32.h"


// --- Definitions for HOTP App ---

// Select how many digits for a key
#define KEY_DIGITS 6

typedef uint64_t counter_t;

typedef struct {
  uint8_t len;
  uint8_t key[64];
  counter_t counter;
} hotp_key_t;

hotp_key_t hotp_key = {0};


// --- Button Handling ---

// Global to keep track of most recently pressed button
int pressed_btn_num;

// Callback for button presses.
//   num: The index of the button associated with the callback
//   val: 1 if pressed, 0 if depressed
static void button_upcall(int                          num,
                          int                          val,
                          __attribute__ ((unused)) int arg2,
                          void *                       ud) {
  if (val == 1) {
    pressed_btn_num = num;
    *((bool*)ud)    = true;
  }
}

// Initializes interrupts on a button
static int initialize_buttons(bool* flag_pointer) {
  // Enable button interrupts
  int err = button_subscribe(button_upcall, (void*)flag_pointer);
  if (err != RETURNCODE_SUCCESS) {
    return err;
  }

  // Determine the number of supported buttons
  int count = 0;
  err = button_count(&count);
  if (err != RETURNCODE_SUCCESS) {
    return err;
  }

  // Enable interrupts if a button exists
  if (count > 0) {
    button_enable_interrupt(0);
  }

  return RETURNCODE_SUCCESS;
}


// --- HMAC Handling ---

static void hmac_upcall(__attribute__ ((unused)) int arg0,
                        __attribute__ ((unused)) int arg1,
                        __attribute__ ((unused)) int arg2,
                        void*                        done_flag) {
  *((bool *) done_flag) = true;
}

static int hmac(const uint8_t* key, int key_len, const uint8_t* data, int data_len, uint8_t* output_buffer,
                int output_buffer_len) {
  int ret;
  bool hmac_done = false;

  ret = hmac_set_callback(hmac_upcall, &hmac_done);
  if (ret < 0) {
    goto done;
  }

  ret = hmac_set_key_buffer(key, key_len);
  if (ret < 0) {
    goto deregister_upcall;
  }

  ret = hmac_set_dest_buffer(output_buffer, output_buffer_len);
  if (ret < 0) {
    goto unallow_key_buffer;
  }

  ret = hmac_set_data_buffer(data, data_len);
  if (ret < 0) {
    goto unallow_dest_buffer;
  }

  ret = hmac_set_algorithm(TOCK_HMAC_ALG_SHA256);
  if (ret < 0) {
    goto unallow_data_buffer;
  }

  ret = hmac_run();
  if (ret < 0) {
    printf("HMAC failure: %d\r\n", ret);
    goto unallow_data_buffer;
  }

  yield_for(&hmac_done);

unallow_data_buffer:
  hmac_set_data_buffer(NULL, 0);

unallow_dest_buffer:
  hmac_set_dest_buffer(NULL, 0);

unallow_key_buffer:
  hmac_set_key_buffer(NULL, 0);

deregister_upcall:
  hmac_set_callback(NULL, NULL);

done:
  return ret;
}

static int decrypt(const uint8_t* cipher, int cipherlen, uint8_t* plaintext, int plaintext_capacity) {
  int copylen = cipherlen;
  if (plaintext_capacity < cipherlen) {
    copylen = plaintext_capacity;
  }
  memcpy(plaintext, cipher, copylen);
  return copylen;
}


// --- HOTP Actions ---

static void program_default_secret(void) {
  led_on(0);
  const char* default_secret = "test";

  // Decode base32 to get HOTP key value
  int ret = base32_decode((const uint8_t*)default_secret, hotp_key.key, 64);
  if (ret < 0 ) {
    printf("ERROR cannot base32 decode secret\r\n");
    hotp_key.len = 0;
    return;
  }

  // Initialize remainder of HOTP key
  hotp_key.len     = ret;
  hotp_key.counter = 0;

  printf("Programmed \"%s\" as key \r\n", default_secret);
  led_off(0);
}

static void get_next_code(void) {
  led_on(0);

  // Decrypt the key
  uint8_t key[64];
  int keylen = decrypt(hotp_key.key, hotp_key.len, key, 64);

  // Generate the HMAC'ed data from the "moving factor" (timestamp in TOTP,
  // counter in HOTP), shuffled in a specific way:
  uint8_t moving_factor[sizeof(counter_t)];
  for (size_t i = 0; i < sizeof(counter_t); i++) {
    moving_factor[i] = (hotp_key.counter >> ((sizeof(counter_t) - i - 1) * 8)) & 0xFF;
  }

  // Perform the HMAC operation
  const uint8_t HMAC_OUTPUT_BUF_LEN = 32;
  uint8_t hmac_output_buf[HMAC_OUTPUT_BUF_LEN];
  hmac(key, keylen, moving_factor, sizeof(counter_t), hmac_output_buf, HMAC_OUTPUT_BUF_LEN);

  // Increment the counter
  hotp_key.counter++;

  // Get output value
  uint8_t offset = hmac_output_buf[HMAC_OUTPUT_BUF_LEN - 1] & 0x0f;
  uint32_t S     = (((hmac_output_buf[offset] & 0x7f) << 24)
                    | ((hmac_output_buf[offset + 1] & 0xff) << 16)
                    | ((hmac_output_buf[offset + 2] & 0xff) << 8)
                    | ((hmac_output_buf[offset + 3] & 0xff)));

  // Limit output to correct number of digits. Modulus by 10^digits
  double digit_count = pow(10, KEY_DIGITS);
  S %= (uint32_t)digit_count;

  // Record value as a string
  char hotp_format_buffer[16];
  int len = snprintf(hotp_format_buffer, 16, "%.*ld", KEY_DIGITS, S);
  if (len < 0) {
    len = 0;
  } else if (len > 16) {
    len = 16;
  }

  // Write the value to the USB keyboard.
  int ret = usb_keyboard_hid_send_string_sync(hotp_format_buffer, len);
  if (ret < 0) {
    printf("ERROR sending string with USB keyboard HID: %i\r\n", ret);
  } else {
    printf("Counter: %u. Typed \"%s\" on the USB HID the keyboard\r\n", (size_t)hotp_key.counter - 1,
           hotp_format_buffer);
  }

  // Complete
  led_off(0);
}


// --- Main Loop ---

// Performs initialization and interactivity.
int main(void) {
  delay_ms(1000);
  printf("Tock HOTP App Started. Usage:\r\n"
      "* Press Button 1 to get the next HOTP code for that slot.\r\n"
      "* Hold Button 1 to enter a new HOTP secret for that slot.\r\n");

  // Initialize buttons
  bool button_pressed = false;
  if (initialize_buttons(&button_pressed) != RETURNCODE_SUCCESS) {
    printf("ERROR initializing buttons\r\n");
    return 1;
  }

  // Configure a default HOTP secret
  program_default_secret();

  // Main loop. Waits for button presses
  while (true) {
    // Yield until a button is pressed
    button_pressed = false;
    yield_for(&button_pressed);

    // Handle short presses on already configured keys (output next code)
    if (hotp_key.len > 0) {
      get_next_code();

      // Error for short press on a non-configured key
    } else if (hotp_key.len == 0) {
      printf("HOTP / TOTP key not yet configured.\r\n");
    }
  }

  return 0;
}
