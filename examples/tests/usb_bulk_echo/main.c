// Licensed under the Apache License, Version 2.0 or the MIT License.
// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright Tock Contributors 2024.

#include <stdio.h>
#include <string.h>

#include <libtock/peripherals/syscalls/usb_syscalls.h>
#include <libtock/peripherals/usb.h>
#include <libtock/tock.h>

// Attribute for unused parameters to silence compiler warnings
#define UNUSED_PARAMETER __attribute__((unused))

// USB device identifiers (same as in the Python test code)
// These are defined in the kernel side and matched by the Python test
// They are kept here for documentation purposes
#define VENDOR_ID 0x6667
#define PRODUCT_ID 0xABCD

// Endpoints (same as in the Python test code)
// These are defined in the kernel side and matched by the Python test
// They are kept here for documentation purposes
#define BULK_IN_EP 0x81  // Endpoint 1, IN direction (0x80)
#define BULK_OUT_EP 0x02 // Endpoint 2, OUT direction (0x00)

// Buffer size for transfers
#define BUFFER_SIZE 8

// Custom driver numbers for USB bulk endpoint functionality
// Note: These would need to match the kernel-side implementation
#define DRIVER_NUM_USB_BULK 0x90006

// Buffer for receiving and echoing data
static uint8_t echo_buffer[BUFFER_SIZE];
static bool data_received = false;

// USB callback functions
static void usb_attach_callback(int status, int unused1, int unused2, void* data);
static void usb_bulk_rx_callback(int callback_type, int unused1, int unused2,
                                 void *opaque);

// Function declarations
static bool setup_usb(void);
static void setup_bulk_endpoints(void);
static void handle_bulk_data(void);

int main(void) {
  printf("Starting USB Bulk Echo Application\n");

  // Initialize and attach USB
  if (!setup_usb()) {
    printf("USB setup failed, exiting\n");
    return -1;
  }

  // Setup bulk endpoints for echo functionality
  setup_bulk_endpoints();

  printf("USB Bulk Echo ready to receive data\n");

  // Main loop: wait for data, then echo it back
  while (1) {
    yield();

    if (data_received) {
      handle_bulk_data();
      data_received = false;
    }
  }

  return 0;
}

// USB attachment callback
static void usb_attach_callback(int status,
                                UNUSED_PARAMETER int unused1,
                                UNUSED_PARAMETER int unused2,
                                UNUSED_PARAMETER void* data) {
  if (status == RETURNCODE_SUCCESS) {
    printf("USB attached successfully\n");
  } else {
    printf("USB attach failed with status %d\n", status);
  }
}

// USB bulk data reception callback
static void usb_bulk_rx_callback(int length,
                                 UNUSED_PARAMETER int unused1,
                                 UNUSED_PARAMETER int unused2,
                                 UNUSED_PARAMETER void *opaque) {
  // Mark that we have received data to be processed
  // length indicates how many bytes were received
  printf("Received %d bytes of USB bulk data\n", length);
  data_received = true;
}

// Set up USB by enabling and attaching to the bus
static bool setup_usb(void) {
  if (!libtock_usb_exists()) {
    printf("Error: USB driver not present\n");
    return false;
  }

  // Register the callback for USB attachment
  returncode_t ret =
      libtock_usb_set_upcall(usb_attach_callback, NULL);
  if (ret != RETURNCODE_SUCCESS) {
    printf("Failed to set USB attachment callback\n");
    return false;
  }

  // Enable and attach USB
  ret = libtock_usb_command_enable_and_attach();
  if (ret != RETURNCODE_SUCCESS) {
    printf("USB enable and attach command failed with status %d\n", ret);
    return false;
  }

  printf("USB initialization complete\n");
  return true;
}

// Check if the USB bulk driver exists
static bool usb_bulk_exists(void) { return driver_exists(DRIVER_NUM_USB_BULK); }

// Set the callback for receiving data on the bulk OUT endpoint
static returncode_t usb_bulk_set_rx_callback(subscribe_upcall callback,
                                             void *data) {
  subscribe_return_t sval = subscribe(DRIVER_NUM_USB_BULK, 0, callback, data);
  return tock_subscribe_return_to_returncode(sval);
}

// Allow buffer for receiving data (OUT endpoint)
static returncode_t usb_bulk_allow_rx_buffer(uint8_t *buffer, uint32_t len) {
  allow_ro_return_t aval = allow_readonly(DRIVER_NUM_USB_BULK, 0, buffer, len);
  return tock_allow_ro_return_to_returncode(aval);
}

// Allow buffer for sending data (IN endpoint)
static returncode_t usb_bulk_allow_tx_buffer(uint8_t *buffer, uint32_t len) {
  allow_rw_return_t aval = allow_readwrite(DRIVER_NUM_USB_BULK, 0, buffer, len);
  return tock_allow_rw_return_to_returncode(aval);
}

// Command to start receiving data on the bulk OUT endpoint
static returncode_t usb_bulk_start_rx(void) {
  syscall_return_t cval = command(DRIVER_NUM_USB_BULK, 1, 0, 0);
  return tock_command_return_novalue_to_returncode(cval);
}

// Command to send data on the bulk IN endpoint
static returncode_t usb_bulk_send_tx(uint32_t len) {
  syscall_return_t cval = command(DRIVER_NUM_USB_BULK, 2, len, 0);
  return tock_command_return_novalue_to_returncode(cval);
}

// Set up bulk endpoints for data transfer
static void setup_bulk_endpoints(void) {
  // Check if the USB bulk driver is available
  if (!usb_bulk_exists()) {
    printf("USB bulk driver not available\n");
    return;
  }

  // Register callback for data reception
  returncode_t ret = usb_bulk_set_rx_callback(usb_bulk_rx_callback, NULL);
  if (ret != RETURNCODE_SUCCESS) {
    printf("Failed to set USB bulk receive callback\n");
    return;
  }

  // Allow read-only buffer for receiving data (OUT endpoint)
  ret = usb_bulk_allow_rx_buffer(echo_buffer, BUFFER_SIZE);
  if (ret != RETURNCODE_SUCCESS) {
    printf("Failed to set USB bulk receive buffer\n");
    return;
  }

  // Start receiving data
  ret = usb_bulk_start_rx();
  if (ret != RETURNCODE_SUCCESS) {
    printf("Failed to start USB bulk receive\n");
    return;
  }

  printf("USB bulk endpoints configured successfully\n");
}

// Handle received bulk data by echoing it back
static void handle_bulk_data(void) {
  // We've received data in the echo_buffer, now send it back

  // Allow read-write buffer for sending data (IN endpoint)
  returncode_t ret = usb_bulk_allow_tx_buffer(echo_buffer, BUFFER_SIZE);
  if (ret != RETURNCODE_SUCCESS) {
    printf("Failed to set USB bulk transmit buffer\n");
    return;
  }

  // Send the data
  ret = usb_bulk_send_tx(BUFFER_SIZE);
  if (ret != RETURNCODE_SUCCESS) {
    printf("Failed to send data via USB bulk endpoint\n");
    return;
  }

  // Get ready to receive more data
  ret = usb_bulk_start_rx();
  if (ret != RETURNCODE_SUCCESS) {
    printf("Failed to restart USB bulk receive\n");
    return;
  }
}
