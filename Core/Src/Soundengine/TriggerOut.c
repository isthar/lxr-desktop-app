/*
 * TriggerOut.c
 *
 *  Created on: 06.05.2013
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */



#include "Soundengine/TriggerOut.h"
#include "string.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "limits.h"
#include "Soundengine/config.h"

#include "main.h"

uint8_t trigger_dividerClockOut1 = PRE_4_PPQ_OUT;
//uint8_t trigger_dividerClockOut2 = PRE_4_PPQ;
uint8_t trigger_prescalerClockInput = PRE_4_PPQ_IN;

uint8_t trigger_nextPulseOut1 = 0xff;
//uint8_t trigger_nextPulseOut2 = 0xff;

/*
uint32_t trigger_pulseTimes[NUM_PINS];
uint8_t trigger_pulseActive[NUM_PINS];
uint8_t trigger_gateMode = 0;
*/

//Clock In Interrupt. In sync mode 1 every pulse goes to the jack sync queue with its
//time stamp; the timing core counts the pulses into steps (4 or 24 PPQ, see
//seq_setClockInPpq()) and measures the tempo from the stamps.
void EXTI4_IRQHandler(void)
{
	//the time stamp first, as close to the edge as possible
	const uint32_t stamp = seqp_stamp();

	/* EXTI line interrupt detected */
	if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_4) != RESET)
	{
		__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_4);

		if(seq_getExtSync() == 1)
		{
			seqp_jackIn(SEQC_EV_PULSE, stamp);
		}
	}
}

//Reset In Interrupt. In sync mode 1 the reset input is a run gate (N4): its edges go to
//the jack sync queue, in order with the clock pulses. Active stops the sequencer and
//rewinds it; released arms a start at the next pulse that begins a step.
void EXTI9_5_IRQHandler(void)
{
	const uint32_t stamp = seqp_stamp();

	/* EXTI line interrupt detected */
	if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_5) != RESET)
	{
		//cleared before the pin is read: an edge after this sets the pending bit again,
		//and the handler runs again and pushes the new level (fw 1.7 cleared it a second
		//time after the read, which could lose an edge and leave the gate stuck)
		__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_5);

		if(seq_getExtSync() == 1)
		{
			//the input is inverted: a low pin means the reset is active
			const uint16_t pinState = (Reset_In_EXTI5_GPIO_Port->IDR & Reset_In_EXTI5_Pin);
			seqp_jackIn(pinState ? SEQC_EV_RESET_OFF : SEQC_EV_RESET_ON, stamp);
		}
	}
}
//--------------------------------------------------
void trigger_setPin(uint8_t index, uint8_t isOn)
{

	switch(index)
	{
	case TRIGGER_1:
	case TRIGGER_2:
	case TRIGGER_3:
	case TRIGGER_4:
	case TRIGGER_5:
	case TRIGGER_6:
	case TRIGGER_7:
	case CLOCK_1:
		/*
		#define Clock_Out_Pin GPIO_PIN_1
		#define Clock_Out_GPIO_Port GPIOA
		*/
		if(isOn)
		{
			Clock_Out_GPIO_Port->ODR |= (Clock_Out_Pin) ;
		} else {
			Clock_Out_GPIO_Port->ODR &= ~(Clock_Out_Pin) ;
		}

		break;


	case CLOCK_2:
	case TRIGGER_RESET:
		/*
		if(!isOn)
		{
			GPIOA->ODR |= (PIN_CLOCK_2<<(index-CLOCK_2)) ;
		} else {
			GPIOA->ODR &= ~(PIN_CLOCK_2<<(index-CLOCK_2)) ;
		}*/
		break;

	case TRIGGER_ALL:
		/*
		if(!isOn)
		{
			GPIOD->ODR |= (PIN_TRACK_1) ;
			GPIOD->ODR |= (PIN_TRACK_1<<1) ;
			GPIOD->ODR |= (PIN_TRACK_1<<2) ;
			GPIOD->ODR |= (PIN_TRACK_1<<3) ;
			GPIOD->ODR |= (PIN_TRACK_1<<4) ;
			GPIOD->ODR |= (PIN_TRACK_1<<5) ;
			GPIOD->ODR |= (PIN_TRACK_1<<6) ;
		} else {
			GPIOD->ODR &= ~(PIN_TRACK_1) ;
			GPIOD->ODR &= ~(PIN_TRACK_1<<1) ;
			GPIOD->ODR &= ~(PIN_TRACK_1<<2) ;
			GPIOD->ODR &= ~(PIN_TRACK_1<<3) ;
			GPIOD->ODR &= ~(PIN_TRACK_1<<4) ;
			GPIOD->ODR &= ~(PIN_TRACK_1<<5) ;
			GPIOD->ODR &= ~(PIN_TRACK_1<<6) ;
		}
		*/
		break;

	default:
		break;
	}

}
//--------------------------------------------------
/*
void trigger_tick()
{
	int i;
	for(i=0;i<NUM_PINS;i++)
	{
		if( (trigger_pulseActive[i]) && (systick_ticks > trigger_pulseTimes[i]) )
		{
			trigger_setPin(i,0);
			trigger_pulseActive[i] = 0;
		}
	}
}
*/
//--------------------------------------------------
/*
void trigger_pulsePin(uint8_t index)
{
	trigger_pulseTimes[index] = systick_ticks + PULSE_LENGTH;

	trigger_pulseActive[index] = 1;
	trigger_setPin(index,1);
}
*/


//--------------------------------------------------
#if 0
void trigger_init()
{

//	memset(trigger_pulseTimes,0,NUM_PINS*sizeof(uint32_t));
//	memset(trigger_pulseActive,0,NUM_PINS);

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;


	//trigger output
	GPIO_InitStructure.GPIO_Pin = PIN_TRACK_1 | PIN_TRACK_2| PIN_TRACK_3 | PIN_TRACK_4 | PIN_TRACK_5 | PIN_TRACK_6 | PIN_TRACK_7 | PIN_CLOCK_1;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = PIN_CLOCK_2 | PIN_RESET;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	//-------------- trigger input ---------------------------------------
	EXTI_InitTypeDef   EXTI_InitStructure;
	NVIC_InitTypeDef   NVIC_InitStructure;
	 /* Enable SYSCFG clock */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

	//---- Reset In (PA8)------------------------------------------------------
	GPIO_InitStructure.GPIO_Pin = PIN_RESET_IN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //maybe floating?
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* Connect EXTI Line8 to PA8 pin */
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource8);

	/* Configure EXTI Line8 */
	EXTI_InitStructure.EXTI_Line = EXTI_Line8;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	/* Enable and set EXTI Line8 Interrupt to the lowest priority */
	NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x0F;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x0F;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);



	//---- Clock In (PC9)------------------------------------------------------
	GPIO_InitStructure.GPIO_Pin = PIN_CLOCK_IN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //maybe floating?
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	/* Connect EXTI Line9 to PC9 pin */
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource9);

	/* Configure EXTI Line9 */
	EXTI_InitStructure.EXTI_Line = EXTI_Line9;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	/* Enable and set EXTI Line9 Interrupt  */
	NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x00;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x0F;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	//---- Timer 2 ------------------------------------------------------
	//Timer 2 (32-bit) for time measuring of external clock pulses (trigger IO)
	TIM_TimeBaseInitTypeDef TIM_TimeBase_InitStructure;
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	//HSE 168MHz, APB1 Prescaler = 4 => 42MHz
	TIM_TimeBase_InitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBase_InitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBase_InitStructure.TIM_Period = 0xffffffff;
	TIM_TimeBase_InitStructure.TIM_Prescaler = 1;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBase_InitStructure);

	TIM_Cmd(TIM2, ENABLE);

	//set trigger outs to low
	int i;
	for(i=0;i<NUM_PINS;i++)
	{
		trigger_setPin(i,0);
	}
}
#endif
//--------------------------------------------------
#if VOICE_TRIGGER_OUTS
void trigger_triggerVoice(uint8_t voice, triggerMode mode)
{
	switch(mode)
	{
	case TRIGGER_PULSE:
		trigger_pulsePin(TRIGGER_1 + voice);
		break;

	case TRIGGER_ON:
		trigger_setPin(TRIGGER_1 + voice,1);
		break;

	case TRIGGER_OFF:
		trigger_setPin(TRIGGER_1 + voice,0);
		break;
	}

}
#endif
//--------------------------------------------------
//cnt [0:23] for a 16 nothe
// => 96ppq (96/4 = 24pulses per 16th note
void trigger_clockTick(uint8_t onOff, uint8_t cnt)
{
#if 1
	uint8_t prescaler = 1;

	switch(trigger_dividerClockOut1)
	{
		case PRE_2_PPQ_OUT:

			prescaler = 48;
			break;

		default:
		case PRE_4_PPQ_OUT:
			prescaler = 24;
			break;

		case PRE_8_PPQ_OUT:
			prescaler = 12;
			break;

		case PRE_16_PPQ_OUT:
			prescaler = 6;
			break;

		case PRE_24_PPQ_OUT:
			prescaler = 4;
			break;

		case PRE_48_PPQ_OUT:
			prescaler = 2;
			break;
	}


	//if(onOff)
	//{

	if(trigger_nextPulseOut1%prescaler==0)
	{
		trigger_setPin(CLOCK_1,1);
	}
	else
	{
		trigger_setPin(CLOCK_1,0);
	}

		trigger_nextPulseOut1++;
		trigger_nextPulseOut1 = trigger_nextPulseOut1%96;

#else
	if(pos==1)
	{
		trigger_nextPulseOut1 = 0;
		//trigger_nextPulseOut2 = 0;
	}

	if(pos >= trigger_nextPulseOut1)
	{
		//trigger_pulsePin(CLOCK_1);
		trigger_setPin(CLOCK_1,1);
		trigger_nextPulseOut1 += trigger_dividerClockOut1;
	} else trigger_setPin(CLOCK_1,0);

	/*
	if(pos >= trigger_nextPulseOut2)
	{
		trigger_setPin(CLOCK_2,1);
		trigger_nextPulseOut2 += trigger_dividerClockOut2;
	}else trigger_setPin(CLOCK_2,0);
	*/
#endif
}
//--------------------------------------------------
/** LICHOMAT: put the clock out divider at pulse 0..95 of the 96 PPQN grid, counted
 * over four steps, before the next trigger_clockTick(). The sequencer calls it with
 * the transport position, so the output stays on the step grid after a Continue
 * from inside a step or after a song position. */
void trigger_setClockPhase(uint8_t pulse)
{
	trigger_nextPulseOut1 = pulse % 96;
}
//--------------------------------------------------
void trigger_reset(uint8_t value)
{
	trigger_setPin(TRIGGER_RESET,1-value);

	if(value) {
		trigger_nextPulseOut1 = 0;
		//trigger_nextPulseOut2 = 0;
	}
	else
	{
		trigger_setPin(CLOCK_1,0);
	}
}
//--------------------------------------------------
/*
uint8_t trigger_isGateModeOn()
{
	return trigger_gateMode;
}
//--------------------------------------------------
void trigger_setGatemode(uint8_t onOff)
{
	trigger_gateMode = onOff;
}
*/
//--------------------------------------------------
void trigger_allOff()
{
	trigger_setPin(TRIGGER_ALL, 0);
}

