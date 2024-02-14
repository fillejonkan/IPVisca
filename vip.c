#include <gio/gio.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/types.h>
#include <string.h>

#include <glib.h>
#include <glib/gprintf.h>


#include "vip.h"
#include "ptz.h"
#include "param.h"

#define VIP_RAW_CMD_START_IDX (8)
#define VIP_RAW_END_MARKER (0xFF)
#define VIP_RAW_RX_DEV_ADDR (0x81)
#define VIP_RAW_TX_DEV_ADDR (0x90)
#define VIP_HEADER_SIZE (8)
#define VIP_MIN_PACKET_SIZE (11)
#define VIP_MIN_INQ_PACKET_SIZE (13)
#define VIP_RAW_CMD_SIZE_IDX (3)

/* Payload type as determined from raw package */
#define VIP_RAW_PT_IDX (VIP_RAW_CMD_START_IDX + 1)

/* Start idx for unique numbers in inquiry */
#define VIP_INC_CMD_START_IDX (VIP_RAW_PT_IDX + 1)

#define SBUF_SIZE (512)

static unsigned char rcv[SBUF_SIZE] = {0,};
static unsigned char raw_cmd[SBUF_SIZE] = {0,};

static int vip_digest_package(unsigned char *buf, size_t len);
static int vip_is_clear_if(unsigned char *buf, size_t len);

/* Inquiry handling functions */
static int vip_digest_inquiry(unsigned char *buf, size_t len);
static int vip_inq_version(unsigned char *buf, size_t len);
static int vip_inq_flip(unsigned char *buf, size_t len);
static int vip_inq_AF(unsigned char *buf, size_t len);
static int vip_inq_PT(unsigned char *buf, size_t len);
static int vip_inq_Zoom(unsigned char *buf, size_t len);

struct vip_endpoint {
  struct sockaddr_in sock_addr;
  int addr_slen;
  int s;
};

struct vip_endpoint cur_endpoint;

/********************************************/

static int vip_inq_version(unsigned char *buf, size_t len)
{
  g_assert(buf);

  /* Fill out raw return buffer */
  buf[VIP_RAW_CMD_START_IDX] = VIP_RAW_TX_DEV_ADDR;
  buf[VIP_RAW_CMD_START_IDX + 1] = 0x50;
  buf[VIP_RAW_CMD_START_IDX + 2] = 0x00;

  buf[VIP_RAW_CMD_START_IDX + 3] = 0x01;
  buf[VIP_RAW_CMD_START_IDX + 4] = 0x05;
  buf[VIP_RAW_CMD_START_IDX + 5] = 0x01;

  buf[VIP_RAW_CMD_START_IDX + 6] = 0x05;
  buf[VIP_RAW_CMD_START_IDX + 7] = 0x00;
  buf[VIP_RAW_CMD_START_IDX + 8] = 0x02;

  buf[VIP_RAW_CMD_START_IDX + 9] = 0xFF;

  return 10;
}

static int vip_inq_flip(unsigned char *buf, size_t len)
{
  g_assert(buf);

  /* Fill out raw return buffer */
  buf[VIP_RAW_CMD_START_IDX] = VIP_RAW_TX_DEV_ADDR;
  buf[VIP_RAW_CMD_START_IDX + 1] = 0x50;

  gboolean rotated = get_rotation();

  if (rotated) {
    g_printf("Image is rotated, flip mode in use\n");
    buf[VIP_RAW_CMD_START_IDX + 2] = 0x02;
  } else {
    g_printf("Image is not rotated, flip not mode in use\n");
    buf[VIP_RAW_CMD_START_IDX + 2] = 0x03;
 }

  buf[VIP_RAW_CMD_START_IDX + 3] = 0xFF;

  return 4;
}

static int vip_inq_AF(unsigned char *buf, size_t len)
{
  g_assert(buf);

  char param[100];
  param_get("PTZ.Various.V1.AutoFocus", param, sizeof(param));

  unsigned int focus_mode = 0x02;
  if (strncmp(param, "true", sizeof(param)) == 0) {
    focus_mode = 0x02;
  } else if (strncmp(param, "false", sizeof(param)) == 0) {
    focus_mode = 0x03;
  } else {
    g_printf("Unknown focus mode! %s\n", param);
  }

  /* Fill out raw return buffer */
  buf[VIP_RAW_CMD_START_IDX] = VIP_RAW_TX_DEV_ADDR;
  buf[VIP_RAW_CMD_START_IDX + 1] = 0x50;
  buf[VIP_RAW_CMD_START_IDX + 2] = focus_mode;
  buf[VIP_RAW_CMD_START_IDX + 3] = 0xFF;

  return 4;
}

static int vip_inq_PT(unsigned char *buf, size_t len)
{
  g_assert(buf);

  struct ptz_status pt;

  get_ptz_status(&pt);

  gboolean rotated = get_rotation();

  unsigned int translated_pan_value;
  if (pt.pan >= 0) {
    translated_pan_value = 0x09BDE * (pt.pan / 170.0f);
    translated_pan_value = CLAMP(translated_pan_value, 0x0000, 0x09BDE);
  } else {
    translated_pan_value = 0xF6422 + ((170.0f + pt.pan) / 170.0f) * 0x09BDE;
    translated_pan_value = CLAMP(translated_pan_value, 0xF6422, 0xFFFFF);
  }

  unsigned int translated_tilt_value;

  g_printf("PTZ STATUS ------ Tilt %f\n", pt.tilt);

  if (rotated) {
    pt.tilt = -pt.tilt;
  }
  
  if (pt.tilt >= 0) {
    translated_tilt_value = 0x52F8 * (pt.tilt / 90.0f);
    translated_tilt_value = CLAMP(translated_tilt_value, 0x0000, 0x52F8);
  } else {
    translated_tilt_value = 0xAD08 + ((90.0f + pt.tilt) / 90.0f) * 0x52F8;
    translated_tilt_value = CLAMP(translated_tilt_value, 0xAD08, 0xFFFF);
  }

//#ifdef VERBOSE
  g_printf("PT INQ!!!!!! --- Translated values: pan %f=0x%04X, tilt %f=0x%04x\n",
    pt.pan, translated_pan_value, pt.tilt, translated_tilt_value);
//#endif

  unsigned int w1 = (translated_pan_value & 0xF0000) >> 16;
  unsigned int w2 = (translated_pan_value & 0x0F000) >> 12;
  unsigned int w3 = (translated_pan_value & 0x00F00) >> 8;
  unsigned int w4 = (translated_pan_value & 0x000F0) >> 4;
  unsigned int w5 = (translated_pan_value & 0x0000F);

  unsigned int z1 = (translated_tilt_value & 0xF000) >> 12;
  unsigned int z2 = (translated_tilt_value & 0x0F00) >> 8;
  unsigned int z3 = (translated_tilt_value & 0x00F0) >> 4;
  unsigned int z4 = (translated_tilt_value & 0x000F);

  /* Fill out raw return buffer */
  buf[VIP_RAW_CMD_START_IDX] = VIP_RAW_TX_DEV_ADDR;
  buf[VIP_RAW_CMD_START_IDX + 1] = 0x50;

  buf[VIP_RAW_CMD_START_IDX + 2] = w1;
  buf[VIP_RAW_CMD_START_IDX + 3] = w2;
  buf[VIP_RAW_CMD_START_IDX + 4] = w3;
  buf[VIP_RAW_CMD_START_IDX + 5] = w4;
  buf[VIP_RAW_CMD_START_IDX + 6] = w5;

  buf[VIP_RAW_CMD_START_IDX + 7] = z1;
  buf[VIP_RAW_CMD_START_IDX + 8] = z2;
  buf[VIP_RAW_CMD_START_IDX + 9] = z3;
  buf[VIP_RAW_CMD_START_IDX + 10] = z4; 

  buf[VIP_RAW_CMD_START_IDX + 11] = 0xFF;  

  return 12;
}

static int vip_inq_Zoom(unsigned char *buf, size_t len)
{
  g_assert(buf);

  struct ptz_status pt;

  get_ptz_status(&pt);

  /* TODO: Disregarding actual zoom factor (30x for V59) and just doing the
   18x scale from Visca document. */
  unsigned int translated_zoom_value = 0x4000 *
    ((pt.zoom - pt.min_zoom) / (pt.max_zoom - pt.min_zoom));

  translated_zoom_value = CLAMP(translated_zoom_value, 0, 0x4000);

  unsigned int p = (translated_zoom_value & 0xF000) >> 12;
  unsigned int q = (translated_zoom_value & 0x0F00) >> 8;
  unsigned int r = (translated_zoom_value & 0x00F0) >> 4;
  unsigned int s = (translated_zoom_value & 0x000F);

#ifdef VERBOSE
  g_printf("Translated values: zoom %f=0x%04X\n",
    pt.zoom, translated_zoom_value);
#endif

  /* Fill out raw return buffer */
  buf[VIP_RAW_CMD_START_IDX] = VIP_RAW_TX_DEV_ADDR;
  buf[VIP_RAW_CMD_START_IDX + 1] = 0x50;

  buf[VIP_RAW_CMD_START_IDX + 2] = p;
  buf[VIP_RAW_CMD_START_IDX + 3] = q;
  buf[VIP_RAW_CMD_START_IDX + 4] = r;
  buf[VIP_RAW_CMD_START_IDX + 5] = s;

  buf[VIP_RAW_CMD_START_IDX + 6] = 0xFF;  

  return 7;
}

static int vip_digest_inquiry(unsigned char *buf, size_t len)
{
  g_assert(len >= VIP_MIN_INQ_PACKET_SIZE);

  if (buf[VIP_INC_CMD_START_IDX] == 0x00 && 
      buf[VIP_INC_CMD_START_IDX + 1] == 0x02) {

    g_printf("Got version inq request\n");
    return vip_inq_version(buf, len);

  } else if (buf[VIP_INC_CMD_START_IDX] == 0x04 && 
             buf[VIP_INC_CMD_START_IDX + 1] == 0x66 ) {

    g_printf("Got Flip mode inq request\n");
    return vip_inq_flip(buf, len);

  } else if (buf[VIP_INC_CMD_START_IDX] == 0x04 && 
             buf[VIP_INC_CMD_START_IDX + 1] == 0x38 ) {

    g_printf("Got AF Mode inq request\n");
    return vip_inq_AF(buf, len);

  } else if (buf[VIP_INC_CMD_START_IDX] == 0x06 && 
             buf[VIP_INC_CMD_START_IDX + 1] == 0x12 ) {

#ifdef VERBOSE
    g_printf("Got PT Position inquiry\n");
#endif
    return vip_inq_PT(buf, len);

  } else if (buf[VIP_INC_CMD_START_IDX] == 0x04 && 
             buf[VIP_INC_CMD_START_IDX + 1] == 0x47 ) {

#ifdef VERBOSE
    g_printf("Got Zoom Position inquiry\n");
#endif
    return vip_inq_Zoom(buf, len);

  } else {
    g_printf("Unhandled VISCA inquiry");
    return -1;
  }
}

static int vip_is_clear_if(unsigned char *buf, size_t len)
{
  if (len != 13) {
    return -1;
  }

  if (buf[9] != 0x01 || buf[10] != 0x00 || buf[11] != 0x01) {
    return -1;
  }

  /* Fill out raw return buffer */
  buf[VIP_RAW_CMD_START_IDX] = VIP_RAW_TX_DEV_ADDR;
  buf[VIP_RAW_CMD_START_IDX + 1] = 0x50;
  buf[VIP_RAW_CMD_START_IDX + 2] = 0xFF;

  return 3;
}

static int vip_digest_package(unsigned char *buf,
                              size_t len)
{
  g_assert(buf);

  int raw_resp_buf_size = -1;

  if (len < VIP_MIN_PACKET_SIZE) {
    return -1;
  }

  if (buf[VIP_RAW_CMD_START_IDX] != VIP_RAW_RX_DEV_ADDR ||
      buf[len-1] != VIP_RAW_END_MARKER) {
    return -1;
  }


  /* VISCA Command, needs ack followed by completion message.
     Tricaster controller does not change header depending on inquiry
     or command, so also check first byte of command. */
  if (buf[0] == 0x01 && buf[1] == 0x00 && buf[VIP_RAW_PT_IDX] == 0x01) {
    //g_printf("Got VISCA command\n");
    
    raw_resp_buf_size = vip_is_clear_if(buf, len);
    
    if (raw_resp_buf_size > 0) {
      g_printf("Got Clear_If, no ack sent\n");

      /* Stop any ongoing Zoom or Pan/Tilt movements */
      stop_continous_movement(TRUE, TRUE);

    } else {
      //g_printf("Got VISCA command, defer to PTZ functionality and first send ack\n");
      size_t raw_cmd_len = buf[VIP_RAW_CMD_SIZE_IDX];

      /* Copy old raw command for procession function */
      memcpy(raw_cmd, &buf[VIP_RAW_CMD_START_IDX], raw_cmd_len);

#ifdef VERBOSE
      g_printf("Data Received: ");
      size_t i = 0;
      for (; i < len; i++) {
        g_printf("%d=[0x%02x], ", i, buf[i]);
      }
      g_printf("\n");
#endif

      /* Send reply to remote end */
      buf[0] = 0x01;
      buf[1] = 0x11;
      buf[2] = 0x00;
      buf[3] = 3;

      /* Fill out raw return buffer for command ACK */
      buf[VIP_RAW_CMD_START_IDX] = VIP_RAW_TX_DEV_ADDR;
      buf[VIP_RAW_CMD_START_IDX + 1] = 0x40;
      buf[VIP_RAW_CMD_START_IDX + 2] = 0xFF;

      size_t resp_size = VIP_HEADER_SIZE + 3;
      
      if (sendto(cur_endpoint.s, 
                 buf,
                 resp_size, 
                 0, 
                 (const struct sockaddr *) &cur_endpoint.sock_addr, 
                 cur_endpoint.addr_slen) == -1) {
        g_printf("Failed to send acknowledgementa\n");
      }
    

#ifdef VERBOSE
      g_printf("Data Sent: ");
      size_t i = 0;
      for (; i < resp_size; i++) {
        g_printf("%d=[0x%02x], ", i, rcv[i]);
      }
      g_printf("\n");

      /* TODO: Process Command */
      g_printf("Procssing cmd length %d\n", raw_cmd_len);
#endif
      process_command(raw_cmd, raw_cmd_len);

      /* Fill out response buffer for command completion */
      buf[VIP_RAW_CMD_START_IDX] = VIP_RAW_TX_DEV_ADDR;
      buf[VIP_RAW_CMD_START_IDX + 1] = 0x50;
      buf[VIP_RAW_CMD_START_IDX + 2] = 0xFF;

      raw_resp_buf_size = 3;
    }
  } 
  /* Second case is for Tricaster where command type is used but inquiry sent anyway */
  else if ((buf[0] == 0x10 && buf[1] == 0x10 && buf[VIP_RAW_PT_IDX] == 0x09) || 
    (buf[0] == 0x01 && buf[1] == 0x00 && buf[VIP_RAW_PT_IDX] == 0x09)) {
    #ifdef VERBOSE
    g_printf("Got VISCA Inquiry\n");
    #endif
    return vip_digest_inquiry(buf, len);
  } else {
    g_printf("Got unhandled VISCA package type\n");
  }

  return raw_resp_buf_size;
}

/********************************************/


int vip_init()
{
  struct sockaddr_in si_me;

  int s;

  if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
    g_printf("Could not create socket!\n");
    return -1;
  }

  memset((char *) &si_me, 0, sizeof(si_me));

  si_me.sin_family = AF_INET;
  si_me.sin_port = htons(52381);
  si_me.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(s, (const struct sockaddr *) &si_me, sizeof(si_me))==-1) {
    g_printf("Failed to bind socket!");
    return -1;
  }

  GIOChannel *channel = g_io_channel_unix_new(s);
  g_io_add_watch(channel, G_IO_IN, (GIOFunc) vip_cmd_callback, GINT_TO_POINTER(s));

  return 0;
}

gboolean vip_cmd_callback(GIOChannel *source,
                          GIOCondition cond,
                          gpointer data)
{
  #ifdef VERBOSE
  g_printf("Received connection from client.\n");
  #endif
  gsize bytes_read;
  cur_endpoint.s = GPOINTER_TO_INT(data);
  int raw_resp_buf_size;

  cur_endpoint.addr_slen = sizeof(cur_endpoint.sock_addr);

  
  bytes_read = recvfrom(cur_endpoint.s, 
                        rcv, 
                        512, 
                        0, 
                        (struct sockaddr *) &cur_endpoint.sock_addr, 
                        (socklen_t *) &cur_endpoint.addr_slen);

  if (bytes_read == -1) {
    g_printf("Failed to receive data!\n");
    goto out;
  }

#ifdef VERBOSE
  g_printf("Received %d bytes packet from %s:%d\n", bytes_read, 
    inet_ntoa(cur_endpoint.sock_addr.sin_addr), 
    ntohs(cur_endpoint.sock_addr.sin_port));

  g_printf("Data Received: ");
  size_t i = 0;
  for (; i < bytes_read; i++) {
    g_printf("%d=[0x%02x], ", i, rcv[i]);
  }
  g_printf("\n");
#endif

  raw_resp_buf_size = vip_digest_package(rcv, bytes_read);

  /* Digest package */
  if (raw_resp_buf_size < 0) {
    g_printf("Invalid Visca command\n");
    goto out;
  }

  size_t resp_size = raw_resp_buf_size + VIP_HEADER_SIZE;

  g_assert(raw_resp_buf_size >= 1 && raw_resp_buf_size <= 16);

  rcv[0] = 0x01;
  rcv[1] = 0x11;
  rcv[2] = 0x00;
  rcv[3] = raw_resp_buf_size;

  /* Send reply to remote end */
  if (sendto(cur_endpoint.s, 
               rcv, 
               resp_size, 
               0, 
               (const struct sockaddr *) &cur_endpoint.sock_addr, 
               cur_endpoint.addr_slen) == -1) {
      g_printf("Failed to echo data\n");
      goto out;
  }

#ifdef VERBOSE
  g_printf("Data Sent: ");
  i = 0;
  for (; i < resp_size; i++) {
    g_printf("%d=[0x%02x], ", i, rcv[i]);
  }
  g_printf("\n");
#endif
  

out:
  return TRUE; 
}