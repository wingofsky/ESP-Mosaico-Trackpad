#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "board.h"
#include "gesture.h"
#include "ui.h"
#include "usb.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static tp_state_t gesture;
static uint32_t reads, touch_frames, errors, gestures, send_failures, no_sample;
static bool board_ok, display_ok, touch_ok, tracing;
static bool pending_release;

static void emit(const tp_report_t *r,void *ctx)
{
    if(!usb_send_mouse(r)) { send_failures++; pending_release=true; }
}
static void status(const tp_input_t *in)
{
    printf("STATUS {\"firmware\":\"%s\",\"board\":%u,\"board_ok\":%s,\"display_ok\":%s,"
           "\"touch_ok\":%s,\"usb_mounted\":%s,\"uptime_ms\":%" PRIu32 ","
           "\"touch_reads\":%" PRIu32 ",\"touch_frames\":%" PRIu32 ",\"gestures\":%" PRIu32 ","
           "\"touch_errors\":%" PRIu32 ",\"no_new_sample\":%" PRIu32 ",\"reports_sent\":%" PRIu32 ",\"reports_completed\":%" PRIu32 ","
           "\"report_failures\":%" PRIu32 ",\"contacts\":%u,\"buttons\":%u}\n",
           esp_app_get_description()->version,board_version(),board_ok?"true":"false",
           display_ok?"true":"false",touch_ok?"true":"false",usb_ready()?"true":"false",
           in->ms,reads,touch_frames,gestures,errors,no_sample,usb_reports_sent(),usb_reports_completed(),
           usb_report_failures(),in->count,gesture.buttons);
}

void app_main(void)
{
    ESP_ERROR_CHECK(usb_init());
    esp_err_t e=board_init();
    board_ok=e==ESP_OK;
    if(board_ok) {
        display_ok=board_display_init()==ESP_OK;
        touch_ok=board_touch_init()==ESP_OK;
        if(display_ok) ESP_ERROR_CHECK(ui_init());
    }
    printf("MOSAICO TRACKPAD %s READY board=%d display=%d touch=%d\n",
           esp_app_get_description()->version,board_ok,display_ok,touch_ok);
    uint32_t last_ui=0,last_status=0,last_trace=0,last_fresh=0;
    uint8_t prev_count=0,button_history=0;
    bool was_connected=false;
    tp_input_t in={0};
    TickType_t tick=xTaskGetTickCount();
    while(1) {
        in.ms=(uint32_t)(esp_timer_get_time()/1000);
        in.valid=false;
        in.count=0;
        if(touch_ok) {
            e=esp_lcd_touch_read_data(board_touch());
            reads++;
            bool fresh=e==ESP_OK;
            if(e==ESP_ERR_NOT_FINISHED) no_sample++;
            if(fresh || e==ESP_ERR_NOT_FINISHED) {
                esp_lcd_touch_point_data_t p[2];
                e=esp_lcd_touch_get_data(board_touch(),p,&in.count,2);
                if(e==ESP_OK) {
                    in.valid=true;
                    for(unsigned i=0;i<in.count;i++) in.p[i]=(tp_point_t){p[i].x,p[i].y,p[i].track_id};
                    if(fresh) {
                        last_fresh=in.ms;
                        if(in.count) touch_frames++;
                    }
                    if(in.count && in.ms-last_fresh>800) { in.valid=false;in.count=0; }
                }
            }
            if(e!=ESP_OK) errors++;
        }
        if(!prev_count && in.count) gestures++;
        button_history=(button_history<<1)|(board_ok && !gpio_get_level(7));
        if((button_history&7)==7) in.button=true;
        else if((button_history&7)==0) in.button=false;
        bool connected=usb_ready();
        if(connected!=was_connected) {
            memset(&gesture,0,sizeof(gesture));
            pending_release=true;
        }
        if(connected && pending_release) {
            tp_report_t release={0};
            if(usb_send_mouse(&release)) {
                memset(&gesture,0,sizeof(gesture));
                gesture.suppress=in.count>0;
                pending_release=false;
            }
        }
        if(connected && !pending_release) tp_process(&gesture,&in,emit,NULL);
        if(display_ok && ((in.count && in.ms-last_ui>=100) || !last_ui ||
                          connected!=was_connected || in.count!=prev_count)) {
            ui_state_t s={.connected=connected,.fault=!touch_ok || !in.valid,
                          .buttons=gesture.buttons,.input=in};
            ui_update(&s);
            last_ui=in.ms;
        }
        if(tracing && (in.count!=prev_count || (in.count && in.ms-last_trace>=50))) {
            printf("TOUCH ms=%" PRIu32 " n=%u p0=%d,%d p1=%d,%d buttons=%u\n",
                   in.ms,in.count,in.p[0].x,in.p[0].y,in.p[1].x,in.p[1].y,gesture.buttons);
            last_trace=in.ms;
        }
        char command[96];
        if(usb_readline(command,sizeof(command))) {
            if(!strcmp(command,"status")) status(&in);
#ifdef CONFIG_TRACKPAD_DIAGNOSTICS
            else if(!strcmp(command,"trace on")) { tracing=true;printf("TRACE ON\n"); }
            else if(!strcmp(command,"trace off")) { tracing=false;printf("TRACE OFF\n"); }
            else if(!strcmp(command,"bootloader")) {
                tp_reset(&gesture,emit,NULL);
                usb_bootloader();
            } else if(!strcmp(command,"hidtest") && !in.count && !in.button && connected) {
                // Explicit diagnostic only: a tiny balanced movement; never a click.
                tp_report_t r={.dx=12};
                bool ok=usb_send_mouse(&r);
                vTaskDelay(pdMS_TO_TICKS(30));
                r.dx=-12;ok=usb_send_mouse(&r)&&ok;
                vTaskDelay(pdMS_TO_TICKS(30));
                r.dx=0;ok=usb_send_mouse(&r)&&ok;
                printf("HIDTEST %s\n",ok?"PASS":"FAIL");
            } else printf("Commands: status | trace on | trace off | hidtest | bootloader\n");
#else
            else printf("Commands: status (development diagnostics disabled)\n");
#endif
        }
        if(in.ms-last_status>=10000) { status(&in);last_status=in.ms; }
        prev_count=in.count;
        was_connected=connected;
        vTaskDelayUntil(&tick,pdMS_TO_TICKS(8));
    }
}
