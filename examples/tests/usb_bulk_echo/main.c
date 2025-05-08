#include <libtock-sync/peripherals/usb.h>
#include <libtock/interface/led.h>
#include <stdio.h>

// Define our USB descriptor values to match the test
#define VENDOR_ID 0x6667
#define PRODUCT_ID 0xabcd

// USB client driver number
#define DRIVER_NUM_USB_CLIENT 0x20003

// USB client command numbers
#define USB_CLIENT_CALLBACK_NOTIFICATION 0
#define USB_CLIENT_ENABLE_ENDPOINTS 1
#define USB_CLIENT_SETUP 2
#define USB_CLIENT_ECHO_BULK 3

// Simple callback function for USB events
static void usb_callback(__attribute__((unused)) returncode_t ret,
                         __attribute__((unused)) uint32_t arg1,
                         __attribute__((unused)) uint32_t arg2) {
  // Just toggle an LED on USB events
  libtock_led_toggle(0);
}

int main(void) {
  returncode_t ret;
  printf("[TEST] USB Bulk Echo\n");

  // Check if USB driver exists
  if (!libtock_usb_exists()) {
    printf("USB test: driver is not present\n");
    return -1;
  }

  // Initialize LED for status indication
  int num_leds = 0;
  libtock_led_count(&num_leds);
  if (num_leds > 0) {
    libtock_led_off(0);
  }

  // Set up callback for USB events
  ret = libtock_subscribe(DRIVER_NUM_USB_CLIENT,
                          USB_CLIENT_CALLBACK_NOTIFICATION, usb_callback, NULL);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to register callback: %d\n", ret);
    return -1;
  }

  // Setup the USB client with our VID/PID
  ret = libtock_command(DRIVER_NUM_USB_CLIENT, USB_CLIENT_SETUP, VENDOR_ID,
                        PRODUCT_ID);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Setup failed with status %d\n", ret);
    return -1;
  }

  // Enable bulk endpoints (1 IN, 2 OUT)
  ret = libtock_command(DRIVER_NUM_USB_CLIENT, USB_CLIENT_ENABLE_ENDPOINTS,
                        1 | (2 << 8), 0);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to enable endpoints: %d\n", ret);
    return -1;
  }

  // Enable the echo functionality in the USB client capsule
  ret = libtock_command(DRIVER_NUM_USB_CLIENT, USB_CLIENT_ECHO_BULK, 0, 0);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to enable bulk echo: %d\n", ret);
    return -1;
  }

  // Attach to USB
  ret = libtocksync_usb_enable_and_attach();
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Attach failed with status %d\n", ret);
    return -1;
  }

  printf("USB test: Enabled and attached with VID=0x%04x PID=0x%04x\n",
         VENDOR_ID, PRODUCT_ID);
  printf("USB test: Bulk echo mode enabled. Any data sent to OUT endpoint 2 "
         "will be echoed on IN endpoint 1.\n");

  // Keep the application running
  while (1) {
    yield();
  }

  return 0;
}
