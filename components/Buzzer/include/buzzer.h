#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define LEDC_GPIO       15
#define LEDC_FREQ       5000
#define LEDC_DUTY       128

class Buzzer {

	public:

	void buzzer_on(void)
	{
		ledc_timer_config_t ledc_timer = {
			.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
			.freq_hz = LEDC_FREQ                // frequency of PWM signal
//			.speed_mode = LEDC_HIGH_SPEED_MODE   // timer mode
		};
		ledc_timer_config(&ledc_timer);

		ledc_channel_config_t ledc_channel = {
				.channel    = LEDC_CHANNEL_0,
				.duty       = 0,
				.gpio_num   = LEDC_GPIO,
				.speed_mode = LEDC_HIGH_SPEED_MODE
		};
		ledc_channel_config(&ledc_channel);

		for (int i = 0; i < 100; i++) {
			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, LEDC_DUTY);
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
			vTaskDelay(3000 / portTICK_PERIOD_MS);

			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
	}
}
