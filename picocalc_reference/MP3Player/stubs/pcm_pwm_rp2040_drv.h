#ifndef PCM_PWM_RP2040_DRV_H
#define PCM_PWM_RP2040_DRV_H

#include "pcm_audio_interface.h"

class pcm_pwm_rp2040_drv : public pcm_audio_interface {
public:
    pcm_pwm_rp2040_drv(int left_pin, int right_pin) {}
};

#endif // PCM_PWM_RP2040_DRV_H
