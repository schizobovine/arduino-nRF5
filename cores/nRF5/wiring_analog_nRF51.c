/*
  Copyright (c) 2014 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifdef NRF51

#include "nrf.h"

#include "Arduino.h"
#include "wiring_private.h"

#ifdef __cplusplus
extern "C" {
#endif

static uint32_t adcReference = ADC_CONFIG_REFSEL_SupplyOneThirdPrescaling;
static uint32_t adcPrescaling = ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling;

NRF_TIMER_Type* pwms[PWM_MODULE_COUNT] = {
  NRF_TIMER1,
  NRF_TIMER2
};

struct PWMContext pwmContext[PWM_COUNT] = {
  { PIN_FREE, 0, TIMER_INTENSET_COMPARE1_Msk, 1, 1, 0 },
  { PIN_FREE, 0, TIMER_INTENSET_COMPARE2_Msk, 2, 2, 0 },
  { PIN_FREE, 0, TIMER_INTENSET_COMPARE3_Msk, 3, 3, 0 },
  { PIN_FREE, 0, TIMER_INTENSET_COMPARE1_Msk, 1, 1, 1 },
  { PIN_FREE, 0, TIMER_INTENSET_COMPARE2_Msk, 2, 2, 1 },
  { PIN_FREE, 0, TIMER_INTENSET_COMPARE3_Msk, 3, 3, 1 }
};

struct PWMStatus pwmStatus[PWM_MODULE_COUNT] = {{0, TIMER1_IRQn},{0, TIMER2_IRQn}};

static uint32_t readResolution = 10;
static uint32_t writeResolution = 8;
static uint32_t halfAnalogWriteMax = 128; // default for 8b
static uint32_t NRF_TIMER_BITMODE = TIMER_BITMODE_BITMODE_08Bit;

void analogReadResolution( int res )
{
  readResolution = res;
}

void analogWriteResolution( int res )
{
  writeResolution = res;

  // TIMER1 has either 16b or 8b PWM resolution
  if ((res > 1) && (res < 17))
  {
    if (res < 9)
    {
      halfAnalogWriteMax = 128;
      NRF_TIMER_BITMODE = TIMER_BITMODE_BITMODE_08Bit;
    }
    else
    {
      halfAnalogWriteMax = 32768; // (2^16) >> 1
      NRF_TIMER_BITMODE = TIMER_BITMODE_BITMODE_16Bit;
    }
  }
  else //default to 8b for any invalid res values
  {
    writeResolution = 8;
    halfAnalogWriteMax = 128;
    NRF_TIMER_BITMODE = TIMER_BITMODE_BITMODE_08Bit;
  }
}

static inline uint32_t mapResolution( uint32_t value, uint32_t from, uint32_t to )
{
  if ( from == to )
  {
    return value ;
  }

  if ( from > to )
  {
    return value >> (from-to) ;
  }
  else
  {
    return value << (to-from) ;
  }
}

/*
 * Internal VBG Reference is 1.2 V.
 * External References AREF0 and AREF1 should be between 0.83 V - 1.3 V.
 *
 * Warning : ADC should not be exposed to > 2.4 V, calculated after prescaling.
 *           GPIO pins must not be exposed to higher voltage than VDD + 0.3 V.
 */
void analogReference( eAnalogReference ulMode )
{
  switch ( ulMode ) {
    case AR_VBG:
      // 1.2 Reference, 1/3 prescaler = 0 V - 3.6 V range
      // Minimum VDD for full range in safe operation = 3.3V
      adcReference = ADC_CONFIG_REFSEL_VBG;
      adcPrescaling = ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling;
      break;

    case AR_SUPPLY_ONE_HALF:
      // 1/2 VDD Reference, 2/3 prescaler = 0 V - 0.75VDD range
      adcReference = ADC_CONFIG_REFSEL_SupplyOneHalfPrescaling;
      adcPrescaling = ADC_CONFIG_INPSEL_AnalogInputTwoThirdsPrescaling;
      break;

    case AR_EXT0:
      // ARF0 reference, 2/3 prescaler = 0 V - 1.5 ARF0
      adcReference = ADC_CONFIG_REFSEL_External | (ADC_CONFIG_EXTREFSEL_AnalogReference0 << ADC_CONFIG_EXTREFSEL_Pos);
      adcPrescaling = ADC_CONFIG_INPSEL_AnalogInputTwoThirdsPrescaling;
      break;

    case AR_EXT1:
      // ARF1 reference, 2/3 prescaler = 0 V - 1.5 ARF1
      adcReference = (ADC_CONFIG_REFSEL_External | ADC_CONFIG_EXTREFSEL_AnalogReference1 << ADC_CONFIG_EXTREFSEL_Pos);
      adcPrescaling = ADC_CONFIG_INPSEL_AnalogInputTwoThirdsPrescaling;
      break;

    case AR_SUPPLY_ONE_THIRD:
    case AR_DEFAULT:
    default:
      // 1/3 VDD Reference, 1/3 prescaler = 0 V - VDD range
      adcReference = ADC_CONFIG_REFSEL_SupplyOneThirdPrescaling;
      adcPrescaling = ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling;
      break;
  }
}

uint32_t analogRead( uint32_t ulPin )
{
  uint32_t pin = ADC_CONFIG_PSEL_Disabled;
  uint32_t adcResolution;
  uint32_t resolution;
  int16_t value;

  if (ulPin >= PINS_COUNT) {
    return 0;
  }

  ulPin = g_ADigitalPinMap[ulPin];

  switch ( ulPin ) {
    case 26:
      pin = ADC_CONFIG_PSEL_AnalogInput0;
      break;

    case 27:
      pin = ADC_CONFIG_PSEL_AnalogInput1;
      break;

    case 1:
      pin = ADC_CONFIG_PSEL_AnalogInput2;
      break;

    case 2:
      pin = ADC_CONFIG_PSEL_AnalogInput3;
      break;

    case 3:
      pin = ADC_CONFIG_PSEL_AnalogInput4;
      break;

    case 4:
      pin = ADC_CONFIG_PSEL_AnalogInput5;
      break;

    case 5:
      pin = ADC_CONFIG_PSEL_AnalogInput6;
      break;

    case 6:
      pin = ADC_CONFIG_PSEL_AnalogInput7;
      break;

    default:
      return 0;
  }

  if (readResolution <= 8) {
    resolution = 8;
    adcResolution = ADC_CONFIG_RES_8bit;
  } else if (readResolution <= 9) {
    resolution = 9;
    adcResolution = ADC_CONFIG_RES_9bit;
  } else {
    resolution = 10;
    adcResolution = ADC_CONFIG_RES_10bit;
  }

  NRF_ADC->ENABLE = 1;

  uint32_t config_reg = 0;

  config_reg |= ((uint32_t)adcResolution << ADC_CONFIG_RES_Pos) & ADC_CONFIG_RES_Msk;
  config_reg |= ((uint32_t)adcPrescaling << ADC_CONFIG_INPSEL_Pos) & ADC_CONFIG_INPSEL_Msk;
  config_reg |= ((uint32_t)adcReference << ADC_CONFIG_REFSEL_Pos) & ADC_CONFIG_REFSEL_Msk;

  if (adcReference & ADC_CONFIG_EXTREFSEL_Msk)
  {
      config_reg |= adcReference & ADC_CONFIG_EXTREFSEL_Msk;
  }

  NRF_ADC->CONFIG = ((uint32_t)pin << ADC_CONFIG_PSEL_Pos) | (NRF_ADC->CONFIG & ~ADC_CONFIG_PSEL_Msk);

  NRF_ADC->CONFIG = config_reg | (NRF_ADC->CONFIG & ADC_CONFIG_PSEL_Msk);

  NRF_ADC->TASKS_START = 1;

  while(!NRF_ADC->EVENTS_END);
  NRF_ADC->EVENTS_END = 0;

  value = (int32_t)NRF_ADC->RESULT;

  NRF_ADC->TASKS_STOP = 1;

  NRF_ADC->ENABLE = 0;

  return mapResolution(value, resolution, readResolution);
}

// Right now, PWM output only works on the pins with
// hardware support.  These are defined in the appropriate
// pins_*.c file.  For the rest of the pins, we default
// to digital output.
void analogWrite( uint32_t ulPin, uint32_t ulValue )
{
  if (ulPin >= PINS_COUNT) {
    return;
  }

  uint32_t ulPin_ = g_ADigitalPinMap[ulPin];

  // Turn off PWM if duty cycle == 0
  if (ulValue == 0)
  {
    for (uint8_t i = 0; i < PWM_COUNT; i++)
    {
      if (pwmContext[i].pin == ulPin_)
      {
        pwmContext[i].pin = PIN_FREE;
        pwmStatus[pwmContext[i].module].numActive--;

        // allocate the pwm channel
        NRF_TIMER_Type* pwm = pwms[pwmContext[i].module];

        // Turn off the PWM module if no pwm channels are allocated
        if (pwmStatus[pwmContext[i].module].numActive == 0)
        {
          NVIC_ClearPendingIRQ(pwmStatus[pwmContext[i].module].irqNumber);
          NVIC_DisableIRQ(pwmStatus[pwmContext[i].module].irqNumber);

          pwm->TASKS_STOP = 1;
          pwm->TASKS_CLEAR = 1;
        }

        digitalWrite(ulPin, 0);
      }
    }

    return;
  }

  for (uint8_t i = 0; i < PWM_COUNT; i++)
  {
    if (pwmContext[i].pin == PIN_FREE || pwmContext[i].pin == ulPin_)
    {
      pwmContext[i].pin = ulPin_;

      NRF_GPIO->PIN_CNF[ulPin_] = ((uint32_t)GPIO_PIN_CNF_DIR_Output       << GPIO_PIN_CNF_DIR_Pos)
                               | ((uint32_t)GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos)
                               | ((uint32_t)GPIO_PIN_CNF_PULL_Disabled    << GPIO_PIN_CNF_PULL_Pos)
                               | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1       << GPIO_PIN_CNF_DRIVE_Pos)
                               | ((uint32_t)GPIO_PIN_CNF_SENSE_Disabled   << GPIO_PIN_CNF_SENSE_Pos);

      // rescale from an arbitrary resolution to either 8b or 16b (transparent to the user)
      // This assumes that the user wont specify an 8b res then pass a 16b res value..
      pwmContext[i].value = mapResolution( ulValue, writeResolution, (writeResolution < 9)?8:16);

      // allocate the pwm channel
      NRF_TIMER_Type* pwm = pwms[pwmContext[i].module];

      // if this is the first channel allocated to the module, turn on pwm
      if (pwmStatus[pwmContext[i].module].numActive == 0)
      {
        NVIC_SetPriority(pwmStatus[pwmContext[i].module].irqNumber, 3);
        NVIC_ClearPendingIRQ(pwmStatus[pwmContext[i].module].irqNumber);
        NVIC_EnableIRQ(pwmStatus[pwmContext[i].module].irqNumber);

        pwm->MODE = (pwm->MODE & ~TIMER_MODE_MODE_Msk) | ((TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos) & TIMER_MODE_MODE_Msk);
        pwm->BITMODE = (pwm->BITMODE & ~TIMER_BITMODE_BITMODE_Msk) | ((NRF_TIMER_BITMODE << TIMER_BITMODE_BITMODE_Pos) & TIMER_BITMODE_BITMODE_Msk);
        pwm->PRESCALER = (pwm->PRESCALER & ~TIMER_PRESCALER_PRESCALER_Msk) | ((7 << TIMER_PRESCALER_PRESCALER_Pos) & TIMER_PRESCALER_PRESCALER_Msk);
        pwm->INTENSET = TIMER_INTENSET_COMPARE0_Msk;
        pwm->CC[0] = 0;
        pwm->TASKS_START = 0x1UL;
      }

      pwm->CC[pwmContext[i].channel] = ulValue;
      pwm->INTENSET |= pwmContext[i].mask;

      pwmStatus[pwmContext[i].module].numActive++;
      return;
    }
  }

  // fallback to digitalWrite if no available PWM channel
  if (ulValue < halfAnalogWriteMax)
  {
    digitalWrite(ulPin, LOW);
  }
  else
  {
    digitalWrite(ulPin, HIGH);
  }
}

void TIMER1_IRQHandler(void)
{
  if (NRF_TIMER1->EVENTS_COMPARE[0]) // channel 0 sets all PWM signals HIGH
  {
    for (uint8_t i = 0; i < PWM_CHANNEL_COUNT; i++)
    {
      if (pwmContext[i].pin != PIN_FREE && pwmContext[i].value != 0)
      {
        NRF_GPIO->OUTSET = (1UL << pwmContext[i].pin);
      }
    }

    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
  }

  for (uint8_t i = 0; i < PWM_CHANNEL_COUNT; i++)  // compare to CC sets the individual PWM signal LOW
  {
    if (NRF_TIMER1->EVENTS_COMPARE[pwmContext[i].event])
    {
      if (pwmContext[i].pin != PIN_FREE && pwmContext[i].value != 2*halfAnalogWriteMax - 1)
      {
        NRF_GPIO->OUTCLR = (1UL << pwmContext[i].pin);
      }

      NRF_TIMER1->EVENTS_COMPARE[pwmContext[i].event] = 0;
    }
  }
}

void TIMER2_IRQHandler(void)
{
  if (NRF_TIMER2->EVENTS_COMPARE[0]) // channel 0 sets all PWM signals HIGH
  {
    for (uint8_t i = PWM_CHANNEL_COUNT; i < 2*PWM_CHANNEL_COUNT; i++)
    {
      if (pwmContext[i].pin != PIN_FREE && pwmContext[i].value != 0)
      {
        NRF_GPIO->OUTSET = (1UL << pwmContext[i].pin);
      }
    }

    NRF_TIMER2->EVENTS_COMPARE[0] = 0;
  }

  for (uint8_t i = PWM_CHANNEL_COUNT; i < 2*PWM_CHANNEL_COUNT; i++)  // compare to CC sets the individual PWM signal LOW
  {
    if (NRF_TIMER2->EVENTS_COMPARE[pwmContext[i].event])
    {
      if (pwmContext[i].pin != PIN_FREE && pwmContext[i].value != 2*halfAnalogWriteMax - 1)
      {
        NRF_GPIO->OUTCLR = (1UL << pwmContext[i].pin);
      }

      NRF_TIMER2->EVENTS_COMPARE[pwmContext[i].event] = 0;
    }
  }
}

#ifdef __cplusplus
}
#endif

#endif
