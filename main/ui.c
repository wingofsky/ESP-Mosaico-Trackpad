#include "ui.h"
#include "board.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static uint16_t *pixels;
static QueueHandle_t states;
// Five columns per glyph, least significant bit at the top; original bitmap font.
static const uint8_t letters[26][5]={
 {126,9,9,9,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {65,65,127,65,65},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {3,4,120,4,3},{97,81,73,69,67}
};
static uint16_t rgb(unsigned r,unsigned g,unsigned b)
{ unsigned c=((r>>3)<<11)|((g>>2)<<5)|(b>>3);return (c>>8)|(c<<8); }
static void rect(int x,int y,int w,int h,uint16_t c)
{
    for(int j=y;j<y+h;j++) for(int i=x;i<x+w;i++)
        if(i>=0 && i<480 && j>=0 && j<480) pixels[j*480+i]=c;
}
static void text(int y,const char *s,int scale,uint16_t c)
{
    int x=(480-(int)strlen(s)*6*scale+scale)/2;
    for(;*s;s++,x+=6*scale) {
        if(*s<'A' || *s>'Z') continue;
        for(int i=0;i<5;i++) for(int j=0;j<7;j++)
            if(letters[*s-'A'][i]&(1<<j)) rect(x+i*scale,y+j*scale,scale,scale,c);
    }
}
static void ring(int x,int y,int radius,uint16_t c)
{
    for(int j=-radius;j<=radius;j++) for(int i=-radius;i<=radius;i++) {
        int d=i*i+j*j;
        if(d<=radius*radius && d>=(radius-3)*(radius-3)) rect(x+i,y+j,1,1,c);
    }
}
static void render(const ui_state_t *s)
{
    uint16_t bg=rgb(11,16,24),fg=rgb(236,242,251),muted=rgb(105,127,151);
    uint16_t accent= s->fault?rgb(255,96,88):s->connected?rgb(81,220,171):rgb(235,187,88);
    rect(0,0,480,480,bg);
    rect(24,24,432,3,accent);
    text(44,"MOSAICO",3,muted);
    text(90,"TRACKPAD",6,fg);
    text(166,s->fault?"TOUCH ERROR":s->connected?"CONNECTED":"CONNECT USB",2,accent);
    // A quiet, continuous pad; all of the screen accepts touch.
    for(int y=220;y<=334;y+=38) for(int x=50;x<460;x+=38) rect(x,y,2,2,rgb(40,53,70));
    if(s->input.count==0) {
        text(252,"SLIDE TO MOVE",2,muted);
    } else {
        for(unsigned i=0;i<s->input.count && i<2;i++)
            ring(s->input.p[i].x,s->input.p[i].y,22,accent);
    }
    rect(24,372,432,1,rgb(43,57,75));
    text(393,s->buttons?"DRAGGING":"TAP TO CLICK",3,s->buttons?accent:fg);
    text(433,"TWO FINGERS TO SCROLL",2,muted);
    esp_err_t err=board_draw(pixels);
    if(err!=ESP_OK) ESP_LOGE("ui","display transfer failed: %s",esp_err_to_name(err));
}
static void task(void *ctx)
{
    ui_state_t s={0};
    while(1) {
        if(xQueueReceive(states,&s,portMAX_DELAY)==pdTRUE) render(&s);
    }
}
esp_err_t ui_init(void)
{
    pixels=heap_caps_aligned_alloc(64,480*480*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!pixels) return ESP_ERR_NO_MEM;
    states=xQueueCreate(1,sizeof(ui_state_t));
    if(!states) return ESP_ERR_NO_MEM;
    return xTaskCreate(task,"trackpad_ui",4096,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
void ui_update(const ui_state_t *s) { if(states) xQueueOverwrite(states,s); }
