// SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// Board pins, CO5300 initialization and power ramp adapted from Espressif
// esp-mosaico-bsp cdf6a398 (Apache-2.0). See THIRD_PARTY.md.
#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board";
static uint16_t revision;
static i2c_master_bus_handle_t bus;
static esp_lcd_panel_handle_t panel;
static esp_lcd_touch_handle_t touch;
static SemaphoreHandle_t draw_done;

static const co5300_lcd_init_cmd_t init_cmds[] = {
    {0x11, NULL, 0, 600}, {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0}, {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0}, {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0}, {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0x80}, 1, 0}, {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00,0x00,0x01,0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00,0x00,0x01,0xDF}, 4, 0},
    {0x29, NULL, 0, 600},
};

static esp_err_t output(int pin, int level, bool od)
{
    gpio_config_t c = {.pin_bit_mask=1ULL<<pin,
                      .mode=od ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_set_level(pin,level),TAG,"preset output");
    ESP_RETURN_ON_ERROR(gpio_config(&c),TAG,"configure output");
    return gpio_hold_dis(pin);
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA,&revision,16),TAG,"board revision");
    ESP_RETURN_ON_FALSE(revision==0x0100 || revision==0x0101 || revision==0x0102,
                        ESP_ERR_NOT_SUPPORTED,TAG,"unknown board 0x%04x",revision);
    ESP_RETURN_ON_ERROR(output(57,1,true),TAG,"release shutdown");
    ESP_RETURN_ON_ERROR(output(60,1,false),TAG,"power rail off");
    ESP_RETURN_ON_ERROR(output(8,0,false),TAG,"motor off");
    if (revision==0x0100) ESP_RETURN_ON_ERROR(output(56,0,false),TAG,"codec off");
    ledc_timer_config_t timer = {.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_8_BIT,
        .timer_num=LEDC_TIMER_1,.freq_hz=100000,.clk_cfg=LEDC_AUTO_CLK};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer),TAG,"rail ramp timer");
    ledc_channel_config_t channel = {.gpio_num=60,.speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_1,.timer_sel=LEDC_TIMER_1,.duty=255};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel),TAG,"rail ramp PWM");
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0),TAG,"rail fade");
    ESP_RETURN_ON_ERROR(ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,0,50),TAG,"rail fade time");
    ESP_RETURN_ON_ERROR(ledc_fade_start(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,LEDC_FADE_WAIT_DONE),TAG,"rail ramp");
    ledc_fade_func_uninstall();
    ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,0);
    ESP_RETURN_ON_ERROR(output(60,0,false),TAG,"rail on");
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_config_t button = {.pin_bit_mask=1ULL<<7,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&button),TAG,"AI button");
    i2c_master_bus_config_t i2c = {.i2c_port=0,.clk_source=I2C_CLK_SRC_DEFAULT,
        .sda_io_num=revision==0x0100 ? 0 : 56,.scl_io_num=revision==0x0100 ? 1 : 3,
        .glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c,&bus),TAG,"I2C bus");
    ESP_LOGI(TAG,"CoreBoard v%u.%u, I2C SDA=%d SCL=%d",revision>>8,revision&255,i2c.sda_io_num,i2c.scl_io_num);
    return ESP_OK;
}

static bool color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken=pdFALSE;
    xSemaphoreGiveFromISR(draw_done,&woken);
    return woken==pdTRUE;
}

esp_err_t board_display_init(void)
{
    draw_done=xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(draw_done,ESP_ERR_NO_MEM,TAG,"draw semaphore");
    int clk=revision==0x0100 ? 44 : 42, reset=revision==0x0100 ? 42 : 44;
    spi_bus_config_t c=CO5300_PANEL_BUS_QSPI_CONFIG(clk,36,51,35,9,480*480*2);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST,&c,SPI_DMA_CH_AUTO),TAG,"LCD SPI bus");
    int pins[]={clk,36,51,35,9};
    for(unsigned i=0;i<5;i++) gpio_set_drive_capability(pins[i],GPIO_DRIVE_CAP_0);
    esp_lcd_panel_io_spi_config_t io=CO5300_PANEL_IO_QSPI_CONFIG(50,color_done,NULL);
    io.flags.psram_dma_direct=true;
    esp_lcd_panel_io_handle_t handle;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io,&handle),TAG,"LCD IO");
    co5300_vendor_config_t vendor={.init_cmds=init_cmds,.init_cmds_size=sizeof(init_cmds)/sizeof(init_cmds[0]),
        .flags.use_qspi_interface=true};
    esp_lcd_panel_dev_config_t p={.reset_gpio_num=reset,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel=16,.vendor_config=&vendor};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(handle,&p,&panel),TAG,"LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel),TAG,"LCD reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel),TAG,"LCD init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel,true),TAG,"LCD on");
    ESP_RETURN_ON_ERROR(board_brightness(45),TAG,"LCD brightness");
    ESP_LOGI(TAG,"CO5300 480x480 ready");
    return ESP_OK;
}

esp_err_t board_touch_init(void)
{
    esp_lcd_panel_io_i2c_config_t c=ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
    c.scl_speed_hz=400000;
    c.transaction_timeout_ms=30;
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus,&c,&io),TAG,"touch IO");
    esp_lcd_touch_config_t t={.x_max=479,.y_max=479,.rst_gpio_num=GPIO_NUM_NC,.int_gpio_num=6,
        .levels={.reset=0,.interrupt=0}};
    return esp_lcd_touch_new_i2c_cst9220(io,&t,&touch);
}

esp_err_t board_draw(const void *pixels)
{
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel,0,0,480,480,pixels),TAG,"draw");
    return xSemaphoreTake(draw_done,pdMS_TO_TICKS(200))==pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t board_brightness(unsigned p) { return esp_lcd_panel_co5300_set_brightness(panel,p); }
esp_lcd_touch_handle_t board_touch(void) { return touch; }
unsigned board_version(void) { return revision; }
