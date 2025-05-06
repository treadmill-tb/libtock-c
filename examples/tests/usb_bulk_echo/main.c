#include <libtock-sync/peripherals/usb.h>
#include <libtock/interface/leds.h>
#include <stdio.h>
#include <string.h>

// Buffer to store data received from USB host
static uint8_t receive_buffer[8];
static uint8_t echo_buffer[8];
static int received_len = 0;

// This callback is triggered when data is received on the bulk OUT endpoint
static returncode_t usb_rx_callback(subscribe_upcall_reply_t response,
                                    const uint8_t *buffer, size_t length) {
  // Turn on LED to indicate data received
  libtock_led_toggle(0);

  // Copy received data to echo buffer
  memcpy(echo_buffer, buffer, length);
  received_len = length;

  printf("USB: Received %d bytes\n", received_len);

  // Queue up the data to be sent back via the IN endpoint
  returncode_t ret = libtocksync_usb_bulk_write(1, echo_buffer, received_len);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB: Failed to queue echo data for transmission: %d\n", ret);
  }

  // Re-subscribe to receive more data
  ret = libtocksync_usb_bulk_subscribe_read(
      2, receive_buffer, sizeof(receive_buffer), usb_rx_callback);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB: Failed to re-subscribe for receiving: %d\n", ret);
  }

  return RETURNCODE_SUCCESS;
}

int main(void) {
  returncode_t ret;
  printf("[TEST] USB Bulk Echo\n");

  // Check if USB driver exists
  if (!libtock_usb_exists()) {
    printf("USB test: driver is not present\n");
    return -1;
  }

  // Enable and attach the USB device
  ret = libtocksync_usb_enable_and_attach();
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Attach failed with status %d\n", ret);
    return -1;
  }
  printf("USB test: Enabled and attached\n");

  // Configure bulk endpoints:
  // Endpoint 1: IN (device-to-host)
  // Endpoint 2: OUT (host-to-device)
  ret = libtocksync_usb_bulk_enable(1, true); // IN endpoint
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to enable bulk IN endpoint: %d\n", ret);
    return -1;
  }

  ret = libtocksync_usb_bulk_enable(2, false); // OUT endpoint
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to enable bulk OUT endpoint: %d\n", ret);
    return -1;
  }

  printf("USB test: Bulk endpoints configured\n");

  // Subscribe to receive data on the OUT endpoint
  ret = libtocksync_usb_bulk_subscribe_read(
      2, receive_buffer, sizeof(receive_buffer), usb_rx_callback);
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB test: Failed to subscribe for receiving: %d\n", ret);
    return -1;
  }

  printf("USB test: Ready for bulk echo test\n");

  // Keep the application running to handle USB events
  while (1) {
    yield();
  }

  return 0;
}
