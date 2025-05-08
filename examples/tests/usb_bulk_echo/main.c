#include <libtock-sync/peripherals/usb.h>
#include <libtock/interface/led.h>
#include <stdio.h>
#include <string.h>

// Define our USB descriptor
#define VENDOR_ID 0x6667
#define PRODUCT_ID 0xabcd

// Buffer to store data received from the USB host
static uint8_t receive_buffer[8];
static uint8_t echo_buffer[8];
static int received_len = 0;

// USB state
static bool usb_ready = false;

// Callback for USB client events (connection state changes)
static void usb_callback(returncode_t __attribute__((unused)) rc, uint32_t type,
                         __attribute__((unused)) uint32_t arg2) {
  if (type == 0) {
    // USB connected
    usb_ready = true;
    printf("USB: Device connected to host\n");
    libtock_led_on(0);
  } else if (type == 1) {
    // USB disconnected
    usb_ready = false;
    printf("USB: Device disconnected from host\n");
    libtock_led_off(0);
  }
}

// Callback for the bulk endpoint
static void bulk_callback(returncode_t __attribute__((unused)) rc,
                          uint32_t endpoint, uint32_t len) {
  // Toggle LED to show activity
  libtock_led_toggle(1);

  printf("USB: Bulk transfer on endpoint %lu, length %lu\n", endpoint, len);

  // If this is a transfer completion on the OUT endpoint (received data)
  if (endpoint == 2) {
    // Copy data to echo buffer (it's already in receive_buffer from the allow)
    memcpy(echo_buffer, receive_buffer, len);
    received_len = len;

    // Echo data back to host via IN endpoint
    libtock_usb_write_ready_callback(1, echo_buffer, received_len);

    // Re-enable receiving on OUT endpoint
    libtock_usb_enable_device_read_callback(2, receive_buffer,
                                            sizeof(receive_buffer));
  }
}

int main(void) {
  returncode_t ret;
  printf("[TEST] USB Bulk Echo\n");

  // Check if USB driver exists
  if (!libtock_usb_exists()) {
    printf("USB test: driver is not present\n");
    return -1;
  }

  // Initialize LEDs for status indication
  int num_leds = 0;
  libtock_led_count(&num_leds);
  if (num_leds > 0) {
    libtock_led_off(0); // LED 0 = USB connection status
    if (num_leds > 1) {
      libtock_led_off(1); // LED 1 = data activity
    }
  }

  // Enable USB and set up the bulk endpoints
  printf("USB test: Enabling USB with bulk endpoints\n");

  // Set up callback for USB state changes
  ret = libtock_usb_subscribe_callback(usb_callback);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to subscribe to USB state changes: %d\n", ret);
    return -1;
  }

  // Set up callback for bulk data transfers
  ret = libtock_usb_subscribe_to_endpoint_callbacks(bulk_callback);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to subscribe to bulk endpoint callbacks: %d\n",
           ret);
    return -1;
  }

  // Set up buffers for receiving data
  ret = libtock_usb_allow_read_callback_buf(receive_buffer,
                                            sizeof(receive_buffer));
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to set up receive buffer: %d\n", ret);
    return -1;
  }

  // Enable the USB device with our vendor/product IDs
  ret = libtocksync_usb_setup_usbc(VENDOR_ID, PRODUCT_ID, 0);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to setup USB: %d\n", ret);
    return -1;
  }

  // Attach to the USB bus
  ret = libtocksync_usb_enable_and_attach();
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Attach failed with status %d\n", ret);
    return -1;
  }

  printf("USB test: USB initialized. Waiting for host connection...\n");

  // Wait for USB to be ready
  while (!usb_ready) {
    yield();
  }

  // Start receiving data on the bulk OUT endpoint
  ret = libtock_usb_enable_device_read_callback(2, receive_buffer,
                                                sizeof(receive_buffer));
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to enable receiving on bulk endpoint: %d\n", ret);
    return -1;
  }

  printf("USB test: Ready for bulk echo test\n");

  // Keep the application running to handle USB events
  while (1) {
    yield();
  }

  return 0;
}
