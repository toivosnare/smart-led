#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "boards/pico_w.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/error.h"
#include "pico/printf.h"
#include "pico/cyw43_arch.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/tcpbase.h"

#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

#define SHA1_SIZE 20

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_FIN (1 << 7)
#define WS_OPCODE 0x0F
#define WS_MASK (1 << 7)
#define WS_PAYLOAD_LEN 0x7F
#define WS_OP_CONTINUATION 0x00
#define WS_OP_TEXT 0x01
#define WS_OP_BINARY 0x02
#define WS_OP_CLOSE 0x08
#define WS_OP_PING 0x09
#define WS_OP_PONG 0x0A

#define LED_GPIO 0
#define REQUEST_BUF_SIZE 512
#define BASE64_ENCODED_SIZE 29
#define PORT 80
#define SSID_SIZE 32
#define PASSWORD_SIZE 64
#define HTTP_RESPONSE_FORMAT "HTTP/1.1 %s\r\nServer: smart-led-server\r\nContent-Length: %zu\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s"

struct WiFiCredentials {
  char ssid[SSID_SIZE];
  char password[PASSWORD_SIZE];
  char padding[160];
};

enum ConnectionState {
  LISTENING,
  HANDSHAKE,
  ONLINE,
};
static enum ConnectionState connection_state = LISTENING;
static struct tcp_pcb *client_pcb = NULL;
static unsigned char ws_frame[3] = {WS_FIN | WS_OP_BINARY, 1, 0};
static bool *led_state = (bool *)&ws_frame[2];

static void send_http_error(const char *status, const char *body) {
  static unsigned char response[256];
  if (connection_state != LISTENING) {
    int response_length = sprintf(response, HTTP_RESPONSE_FORMAT, status, strlen(body), body);
    tcp_write(client_pcb, response, response_length, 0);
    tcp_output(client_pcb);
  }
}

static void send_led_state(void) {
  if (connection_state == ONLINE) {
    printf("Sending LED state (%s) to client.\n", *led_state ? "on" : "off");
    tcp_write(client_pcb, ws_frame, 3, 0);
    tcp_output(client_pcb);
  }  
}

static void handle_handshake(struct pbuf *p) {
  static unsigned char request_buf[REQUEST_BUF_SIZE];
  static size_t request_buf_length = 0;

  unsigned char *buf_end = &request_buf[request_buf_length];
  size_t space_left = REQUEST_BUF_SIZE - request_buf_length;
  u16_t n = pbuf_copy_partial(p, buf_end, space_left, 0);
  request_buf_length += n;

  const unsigned char *header_end = memmem(buf_end, n, "\r\n\r\n", 4);
  if (!header_end)
    return;

  if (request_buf_length < 16 || memcmp(request_buf, "GET / HTTP/1.1\r\n", 16)) {
    printf("Invalid handshake request.\n");
    send_http_error("400 Bad Request", "Invalid status line.");
    request_buf_length = 0;
    return;
  }

  unsigned char *key = &request_buf[16], *value;
  size_t key_length, value_length;
  unsigned char *c;
  bool connection_upgrade = false;
  bool upgrade_websocket = false;
  unsigned char *websocket_key = NULL;
  size_t websocket_key_length = 0;

  while (key < header_end) {
    c = key;
    while (*c != ':') {
      *c = tolower(*c);
      ++c;
    }
    key_length = c - key;
    value = c + 2;

    c = value;
    while (c[0] != '\r' || c[1] != '\n') ++c;
    value_length = c - value;

    // printf("key=%.*s, value=%.*s\n", key_length, key, value_length, value);
    if (key_length == 10 && !memcmp(key, "connection", 10)
        && value_length == 7 && !memcmp(value, "Upgrade", 7))
      connection_upgrade = true;
    if (key_length == 7 && !memcmp(key, "upgrade", 7)
        && value_length == 9 && !memcmp(value, "websocket", 9))
      upgrade_websocket = true;
    if (key_length == 17 && !memcmp(key, "sec-websocket-key", 17)) {
      websocket_key = value;
      websocket_key_length = value_length;
    }

    key = c + 2;
  }

  if (!connection_upgrade || !upgrade_websocket || !websocket_key) {
    printf("Invalid handshake request.\n");
    send_http_error("400 Bad Request", "Only websocket upgrades supported.");
    request_buf_length = 0;
    return;
  }
  
  // Concatenate websocket key and GUID.
  unsigned char *websocket_key_end = websocket_key + websocket_key_length;
  strcpy(websocket_key_end, WS_GUID);

  size_t hash_input_length = websocket_key_length + strlen(WS_GUID);
  unsigned char hash_output[SHA1_SIZE];
  mbedtls_sha1(websocket_key, hash_input_length, hash_output);

  unsigned char base64_encoded_output[BASE64_ENCODED_SIZE];
  size_t base64_encoded_length;
  mbedtls_base64_encode(base64_encoded_output, BASE64_ENCODED_SIZE, &base64_encoded_length, hash_output, SHA1_SIZE);

  const char *headers = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
  tcp_write(client_pcb, headers, strlen(headers), TCP_WRITE_FLAG_MORE);
  tcp_write(client_pcb, base64_encoded_output, base64_encoded_length, TCP_WRITE_FLAG_MORE | TCP_WRITE_FLAG_COPY);
  tcp_write(client_pcb, "\r\n\r\n", 4, 0);

  printf("Valid handshake request received. Sending response to client.\n");
  tcp_output(client_pcb);
  connection_state = ONLINE;
  request_buf_length = 0;
  
  // If LED is on send info to client to update the UI.
  if (*led_state)
    send_led_state();
}

static void send_websocket_close_frame(void) {
  static unsigned char close_frame[2] = {WS_FIN | WS_OP_CLOSE, 0};
  if (connection_state == ONLINE) {
    tcp_write(client_pcb, close_frame, 2, 0);
    tcp_output(client_pcb);
  }
}

static void set_led_state(bool on) {
  if (on != *led_state) {
    // TODO: use pico GPIO LED instead of cyw43 LED?
    printf("Turning LED %s.\n", on ? "on" : "off");
    cyw43_arch_gpio_put(LED_GPIO, on);
    *led_state = on;
  }
  send_led_state();
}

static void handle_online(struct pbuf *p) {
  static unsigned char request_buf[REQUEST_BUF_SIZE];
  static size_t request_buf_length = 0;

  u16_t n = pbuf_copy_partial(p, &request_buf[request_buf_length], p->tot_len, 0);
  request_buf_length += n;
  if (request_buf_length < 2)
    return;

  bool fin = request_buf[0] & WS_FIN;
  bool mask = request_buf[1] & WS_MASK;
  unsigned char payload_length = request_buf[1] & WS_PAYLOAD_LEN;

  if (!fin || !mask || payload_length != 1) {
    printf("Received invalid websocket frame.\n");
    send_websocket_close_frame();
    tcp_close(client_pcb);
    client_pcb = NULL;
    connection_state = LISTENING;
    request_buf_length = 0;
    return;
  }

  size_t frame_length = payload_length + 6;
  if (request_buf_length < frame_length)
    return;

  unsigned char opcode = request_buf[0] & WS_OPCODE;
  // Only the first byte of the masking key is need as the payload is always
  // just one byte.
  unsigned char masking_key = request_buf[2];

  // for (size_t i = 0; i < request_buf_length; ++i)
  //   printf("%x ", request_buf[i]);
  // printf("\n");

  if (opcode == WS_OP_BINARY) {
    bool value = request_buf[6] ^ masking_key;
    printf("Received request to turn LED %s.\n", value ? "on" : "off");
    set_led_state(value);
  } else if (opcode == WS_OP_CLOSE) {
    printf("Received close frame.\n");
    send_websocket_close_frame();
    tcp_close(client_pcb);
    client_pcb = NULL;
    connection_state = LISTENING;
    request_buf_length = 0;
    return;
  } else {
    printf("Received frame with invalid opcode %u.\n", opcode);
    send_websocket_close_frame();
    tcp_close(client_pcb);
    client_pcb = NULL;
    connection_state = LISTENING;
    request_buf_length = 0;
    return;
  }

  // There could be extra bytes in the buffer that belong to the next frame.
  size_t extra_bytes = request_buf_length - frame_length;
  if (extra_bytes)
    memmove(request_buf, &request_buf[frame_length], extra_bytes);
  request_buf_length = extra_bytes;
}

static err_t recv_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  if (connection_state == LISTENING) {
    printf("How did we end up here?\n");
    return ERR_OK;
  }
  if (pcb != client_pcb) {
    printf("PCBs not matching?\n");
    return ERR_OK;
  }
  if (!p) {
    printf("Connection closed.\n");
    tcp_close(client_pcb);
    client_pcb = NULL;
    connection_state = LISTENING;
    return ERR_OK;
  }
  if (connection_state == HANDSHAKE)
    handle_handshake(p);
  else
    handle_online(p);

  pbuf_free(p);
  return ERR_OK;
}

static void err_callback(void *arg, err_t err) {
  printf("Error code %d.\n", err);
}

static err_t accept_callback(void *arg, struct tcp_pcb *pcb, err_t err) {
  if (pcb == NULL || err != ERR_OK)  {
      printf("Failure in accept.\n");
      return ERR_VAL;
  }
  if (connection_state != LISTENING) {
    printf("Not in listening state.\n");
    tcp_close(pcb);
    return ERR_OK;
  }
  printf("Client connected.\n");
  connection_state = HANDSHAKE;
  client_pcb = pcb;
  tcp_recv(client_pcb, recv_callback);
  tcp_err(client_pcb, err_callback);
  return ERR_OK;
}

static size_t get_string(char *dst, size_t limit) {
  int c;
  size_t i = 0;
  while (i < limit - 1) {
    c = getchar();
    putchar(c);
    if (c == '\r' || c == '\n')
      break;
    dst[i++] = c;
  }
  if (i > 0 && dst[i - 1] == '\r')
    --i;
  dst[i] = '\0';
  return i;
}

static void connect(void) {
  // Find first unprogrammed page from the last sector
  // (starting from the second page).
  unsigned char *p = (unsigned char *)(XIP_BASE + PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE + FLASH_PAGE_SIZE);
  for (; p < (unsigned char *)(XIP_BASE + PICO_FLASH_SIZE_BYTES); p += FLASH_PAGE_SIZE) {
    if (*(int *)p == -1)
      break;
  }

  // The previous page must be the last programmed page.
  struct WiFiCredentials *creds = (struct WiFiCredentials *)(p - FLASH_PAGE_SIZE);
  bool flash_empty = true;
  for (size_t i = 0; i < SSID_SIZE; ++i) {
    if (creds->ssid[i] != 0xFF) {
      flash_empty = false;
      break;
    }
  }
  if (!flash_empty) {
    printf("Found credentials in the flash.\n");
    if (!cyw43_arch_wifi_connect_timeout_ms(creds->ssid, creds->password, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
      printf("Connected.\n");
      return;
    }
  }

  struct WiFiCredentials new_creds;
  do {
    printf("Connection failed!\nEnter WiFi SSID: ");
    get_string(new_creds.ssid, SSID_SIZE);
    printf("\nEnter WiFi password: ");
    get_string(new_creds.password, PASSWORD_SIZE);
    printf("\n");
  } while (cyw43_arch_wifi_connect_timeout_ms(new_creds.ssid, new_creds.password, CYW43_AUTH_WPA2_AES_PSK, 30000));
  printf("Connected.\n");

  uint32_t flash_offset;
  uint32_t interrupts = save_and_disable_interrupts();
  if (p >= (unsigned char *)XIP_BASE + PICO_FLASH_SIZE_BYTES) {
    printf("Erasing flash.\n");
    flash_range_erase(PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    printf("Erase complete.\n");
    flash_offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
  } else {
    flash_offset = (uint32_t)(p - XIP_BASE);
  }
  printf("Programming flash.\n");
  flash_range_program(flash_offset, (const uint8_t *)&new_creds, FLASH_PAGE_SIZE);
  printf("Programming complete.\n");
  restore_interrupts(interrupts);
}

int main() {
  // TODO: Setup GPIO LED and button (with interrupts?).
  stdio_init_all();
  if (cyw43_arch_init()) {
    printf("Failed to initialize.\n");
    return 1;
  }
  cyw43_arch_enable_sta_mode();
  connect();

  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!pcb) {
    printf("Failed to create pcb.\n");
    return 1;
  }

  err_t err = tcp_bind(pcb, IP_ANY_TYPE, PORT);
  if (err) {
    printf("Failed to bind.\n");
    return 1;
  }
  
  pcb = tcp_listen_with_backlog(pcb, 1);
  if (!pcb) {
    printf("Failed to listen.\n");
    return 1;
  }
  tcp_accept(pcb, accept_callback);

  while (true) {
    cyw43_arch_poll();
    sleep_ms(1);
  }

  return 0;
}
