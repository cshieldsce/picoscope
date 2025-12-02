#include "test_signal.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"

void vTestSignalInit(void) {
    const uint PWM_PIN = 15;
    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(PWM_PIN);

    pwm_config config = pwm_get_default_config();
    // 125MHz system clock / 100 = 1.25MHz PWM clock
    pwm_config_set_clkdiv(&config, 100.0f);
    // 1.25MHz / 1250 cycles = 1 kHz frequency
    pwm_config_set_wrap(&config, 1249); 
    pwm_init(slice_num, &config, true);

    // Set 50% duty cycle (1250 / 2 = 625)
    pwm_set_gpio_level(PWM_PIN, 625);
}