#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "mobus cllient";

#include <joystick.h>
#include <oled.hpp>
#include <buzzer.h>
#include <wifi.hpp>
// #include <ota.hpp>


extern "C" {    
    void app_main();
}

void app_main(void) {
    
    printf("Hello world!!!!\n");
  
    Oled oled;
    MenuDisplay menu;
    TalkDisplay talk;

    oled.BootDisplay();

	Buzzer buzzer;
	buzzer.buzzer_on();
    
    // WIP
    // oled.ShowImage();
    // vTaskDelay(5000 / portTICK_PERIOD_MS);

    WiFi wifi;
    wifi.main();
 
    // FIXME
    //for (int i = 0; i <= 10; i++) {
    //    WiFi::wifi_state_t wifi_state = wifi.get_wifi_state();
    //    printf("Wifi state:%c\n",wifi_state.state);
    //    vTaskDelay(1000 / portTICK_PERIOD_MS);
    //}

    // TODO:menuから各機能の画面に遷移するように実装する
    // menu.Menu();
    talk.Talk();

    /* Print chip information */
    esp_chip_info_t chip_info;

    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ", CONFIG_IDF_TARGET,
           chip_info.cores, (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);


    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
                                                         : "external");

    printf("Minimum free heap size: %d bytes\n",
           esp_get_minimum_free_heap_size());

    gpio_set_direction(GPIO_NUM_25, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_26, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_INPUT);

    for (int i = 0; i <= 10; i++) {
        printf(" GPIO25=%d", gpio_get_level(GPIO_NUM_25));
        printf(" GPIO26=%d", gpio_get_level(GPIO_NUM_26));
        printf(" GPIO4=%d\n", gpio_get_level(GPIO_NUM_4));
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
