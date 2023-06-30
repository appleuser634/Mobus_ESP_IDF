#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define BUZZER_GPIO       18
#define BUZZER_DUTY       128

class Buzzer {

	public:
	
	u_int32_t LEDC_FREQ = 1200;
	u_int32_t STEP = 10;
		
	ledc_timer_config_t ledc_timer;
	ledc_channel_config_t ledc_channel;

	Buzzer(void) {
		ledc_timer_config_t ledc_timer = {
			.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
			.freq_hz = LEDC_FREQ                // frequency of PWM signal
			// .speed_mode = LEDC_HIGH_SPEED_MODE   // timer mode
		};
		ledc_timer_config(&ledc_timer);

		ledc_channel_config_t ledc_channel = {
				.gpio_num   = BUZZER_GPIO,
				.speed_mode = LEDC_HIGH_SPEED_MODE
				// .channel    = 0,
				// .duty       = 0,
		};
		ledc_channel_config(&ledc_channel);
	}

	void ring_sound(u_int32_t freq, int time, int duty=BUZZER_DUTY)
	{
		ledc_timer_config_t ledc_timer = {
			.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
			.freq_hz = freq                // frequency of PWM signal
			// .speed_mode = LEDC_HIGH_SPEED_MODE   // timer mode
		};
		ledc_timer_config(&ledc_timer);

		ledc_channel_config_t ledc_channel = {
				.gpio_num   = BUZZER_GPIO,
				.speed_mode = LEDC_HIGH_SPEED_MODE
				// .channel    = 0,
				// .duty       = 0,
		};
		ledc_channel_config(&ledc_channel);

		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, BUZZER_DUTY);
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
		vTaskDelay(time / portTICK_PERIOD_MS);
	}


	void buzzer_on(void) {
		ledc_timer_config_t ledc_timer = {
			.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
			.freq_hz = LEDC_FREQ                // frequency of PWM signal
			// .speed_mode = LEDC_HIGH_SPEED_MODE   // timer mode
		};
		ledc_timer_config(&ledc_timer);

		ledc_channel_config_t ledc_channel = {
				.gpio_num   = BUZZER_GPIO,
				.speed_mode = LEDC_HIGH_SPEED_MODE
				// .channel    = 0,
				// .duty       = 0,
		};
		ledc_channel_config(&ledc_channel);

		// 指定の周波数でPWM
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, BUZZER_DUTY);
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	}
	
	void buzzer_off(void) {
		ledc_timer_config_t ledc_timer = {
			.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
			.freq_hz = 0                // frequency of PWM signal
			// .speed_mode = LEDC_HIGH_SPEED_MODE   // timer mode
		};
		ledc_timer_config(&ledc_timer);

		ledc_channel_config_t ledc_channel = {
				.gpio_num   = BUZZER_GPIO,
				.speed_mode = LEDC_HIGH_SPEED_MODE
				// .channel    = 0,
				// .duty       = 0,
		};
		ledc_channel_config(&ledc_channel);

		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	};
	
	void boot_sound(void)
	{
		// ring_sound(1200, 100);
		// ring_sound(0, 50, 0);
		// ring_sound(1200, 100);
		// ring_sound(0, 50, 0);

		ledc_timer_config_t ledc_timer = {
			.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
			.freq_hz = LEDC_FREQ                // frequency of PWM signal
			// .speed_mode = LEDC_HIGH_SPEED_MODE   // timer mode
		};
		ledc_timer_config(&ledc_timer);

		ledc_channel_config_t ledc_channel = {
				.gpio_num   = BUZZER_GPIO,
				.speed_mode = LEDC_HIGH_SPEED_MODE
				// .channel    = 0,
				// .duty       = 0,
		};
		ledc_channel_config(&ledc_channel);

		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, BUZZER_DUTY);
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
		vTaskDelay(100 / portTICK_PERIOD_MS);

		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
		vTaskDelay(50 / portTICK_PERIOD_MS);
		
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, BUZZER_DUTY);
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
		vTaskDelay(100 / portTICK_PERIOD_MS);
		
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	}


	void recv_sound(void)
	{
			ledc_timer_config_t ledc_timer = {
				.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
				.freq_hz = LEDC_FREQ                // frequency of PWM signal
				// .speed_mode = LEDC_HIGH_SPEED_MODE   // timer mode
			};
			ledc_timer_config(&ledc_timer);

			ledc_channel_config_t ledc_channel = {
					.gpio_num   = BUZZER_GPIO,
					.speed_mode = LEDC_HIGH_SPEED_MODE
					// .channel    = 0,
					// .duty       = 0,
			};
			ledc_channel_config(&ledc_channel);

			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 1200);
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
			vTaskDelay(100 / portTICK_PERIOD_MS);

			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
			vTaskDelay(50 / portTICK_PERIOD_MS);
			
			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 1300);
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
			vTaskDelay(100 / portTICK_PERIOD_MS);
			
			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);

			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 1400);
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
			vTaskDelay(100 / portTICK_PERIOD_MS);
			
			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	}

	void right_sound(void)
	{

		ledc_timer_config_t ledc_timer = {
			.duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
			.freq_hz = LEDC_FREQ                // frequency of PWM signal
			// .speed_mode = LEDC_HIGH_SPEED_MODE   // timer mode
		};
		ledc_timer_config(&ledc_timer);

		ledc_channel_config_t ledc_channel = {
				.gpio_num   = BUZZER_GPIO,
				.speed_mode = LEDC_HIGH_SPEED_MODE
				// .channel    = 0,
				// .duty       = 0,
		};
		ledc_channel_config(&ledc_channel);

		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, BUZZER_DUTY);
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
		vTaskDelay(100 / portTICK_PERIOD_MS);

		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
		vTaskDelay(50 / portTICK_PERIOD_MS);
		
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, BUZZER_DUTY);
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
		vTaskDelay(100 / portTICK_PERIOD_MS);
		
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); //duty=0 で消音
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	}


};

