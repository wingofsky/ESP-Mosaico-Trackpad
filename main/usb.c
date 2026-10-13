#include "usb.h"
#include "command_line.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"

static const uint8_t report_desc[]={TUD_HID_REPORT_DESC_MOUSE()};
enum { CDC_CONTROL, CDC_DATA, HID_MOUSE, INTERFACE_COUNT };
#define DESC_LEN (TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN+TUD_HID_DESC_LEN)
static const uint8_t config_fs[]={
    TUD_CONFIG_DESCRIPTOR(1,INTERFACE_COUNT,0,DESC_LEN,0,250),
    TUD_CDC_DESCRIPTOR(CDC_CONTROL,4,0x81,8,0x02,0x82,64),
    TUD_HID_DESCRIPTOR(HID_MOUSE,5,HID_ITF_PROTOCOL_NONE,sizeof(report_desc),0x83,16,4),
};
static const uint8_t config_hs[]={
    TUD_CONFIG_DESCRIPTOR(1,INTERFACE_COUNT,0,DESC_LEN,0,250),
    TUD_CDC_DESCRIPTOR(CDC_CONTROL,4,0x81,8,0x02,0x82,512),
    TUD_HID_DESCRIPTOR(HID_MOUSE,5,HID_ITF_PROTOCOL_NONE,sizeof(report_desc),0x83,16,6),
};
static const tusb_desc_device_t device_desc={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
    .bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,.bDeviceProtocol=MISC_PROTOCOL_IAD,
    .bMaxPacketSize0=CFG_TUD_ENDPOINT0_SIZE,.idVendor=0x303a,.idProduct=0x4005,.bcdDevice=0x0100,
    .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1,
};
static char serial[16];
static const char *strings[]={(char[]){0x09,0x04},"Mosaico Trackpad","Mosaico Trackpad",serial,
                            "Trackpad diagnostics","Touch mouse"};
static atomic_uint sent, completed, failures;

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return report_desc; }
uint16_t tud_hid_get_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t *buffer,uint16_t n)
{
    if(type!=HID_REPORT_TYPE_INPUT || id!=0 || n<5) return 0;
    memset(buffer,0,5);
    return 5;
}
void tud_hid_set_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,const uint8_t *b,uint16_t n) {}
void tud_hid_report_complete_cb(uint8_t instance,const uint8_t *report,uint16_t len) { atomic_fetch_add(&completed,1); }

esp_err_t usb_init(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_STA));
    snprintf(serial,sizeof(serial),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    tinyusb_config_t c=TINYUSB_DEFAULT_CONFIG();
    c.descriptor.device=&device_desc;
    c.descriptor.string=strings;
    c.descriptor.string_count=sizeof(strings)/sizeof(strings[0]);
    c.descriptor.full_speed_config=config_fs;
    c.descriptor.high_speed_config=config_hs;
    ESP_ERROR_CHECK(tinyusb_driver_install(&c));
    tinyusb_config_cdcacm_t cdc={.cdc_port=TINYUSB_CDC_ACM_0};
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&cdc));
    ESP_ERROR_CHECK(tinyusb_console_init(TINYUSB_CDC_ACM_0));
    setvbuf(stdout,NULL,_IONBF,0);
    setvbuf(stderr,NULL,_IONBF,0);
    return ESP_OK;
}

bool usb_ready(void) { return tud_mounted() && !tud_suspended(); }
static int clamp127(int n) { return n>127 ? 127 : n < -127 ? -127 : n; }
bool usb_send_mouse(const tp_report_t *r)
{
    int x=r->dx,y=r->dy;
    bool first=true;
    do {
        int64_t end=esp_timer_get_time()+50000;
        while(usb_ready() && !tud_hid_ready() && esp_timer_get_time()<end) vTaskDelay(pdMS_TO_TICKS(1));
        int dx=clamp127(x),dy=clamp127(y);
        if(!usb_ready() || !tud_hid_ready() ||
           !tud_hid_mouse_report(0,r->buttons,dx,dy,first?r->wheel:0,first?r->pan:0)) {
            atomic_fetch_add(&failures,1);
            return false;
        }
        atomic_fetch_add(&sent,1);
        x-=dx;y-=dy;first=false;
    } while(x || y);
    return true;
}

static bool read_command_byte(char *ch,void *context)
{
    size_t n=0;
    return tud_cdc_available() &&
           tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0,(uint8_t *)ch,1,&n)==ESP_OK && n==1;
}
bool usb_readline(char *out,size_t cap)
{
    static command_line_t state;
    return command_readline(&state,out,cap,read_command_byte,NULL);
}
uint32_t usb_reports_sent(void) { return atomic_load(&sent); }
uint32_t usb_reports_completed(void) { return atomic_load(&completed); }
uint32_t usb_report_failures(void) { return atomic_load(&failures); }
#ifdef CONFIG_TRACKPAD_DIAGNOSTICS
static void force_download(void) { REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG,LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT); }
void usb_bootloader(void)
{
    printf("BOOTLOADER\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_register_shutdown_handler(force_download));
    esp_restart();
}
#endif
