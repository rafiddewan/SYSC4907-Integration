#include "timers.h"
#include "LEDs.h"
#include "delay.h"
#include "ultrasonic.h"
#include "MKL25Z4.h"

volatile unsigned PIT_interrupt_counter = 0;
volatile unsigned LCD_update_requested = 0;

volatile unsigned overflow = 0;
volatile unsigned echoFallingEdge = 0;
volatile int ticksElapsed = 0;
volatile unsigned measureFlag = 0;
volatile int timeoutFlag = 0; 

volatile extern int tpm0_flag;

void Init_PITs(unsigned period1, unsigned period2) {
	// Enable clock to PIT module
	SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;
	
	// Enable module, freeze timers in debug mode
	PIT->MCR &= ~PIT_MCR_MDIS_MASK;
	PIT->MCR |= PIT_MCR_FRZ_MASK;
	
	// Initialize PITs to count down from argument 
	PIT->CHANNEL[0].LDVAL = PIT_LDVAL_TSV(period1);
	PIT->CHANNEL[1].LDVAL = PIT_LDVAL_TSV(period2);

	// No chaining
	PIT->CHANNEL[0].TCTRL &= PIT_TCTRL_CHN_MASK;
	PIT->CHANNEL[1].TCTRL &= PIT_TCTRL_CHN_MASK;
	
	// Generate interrupts
	PIT->CHANNEL[0].TCTRL |= PIT_TCTRL_TIE_MASK;
	PIT->CHANNEL[1].TCTRL |= PIT_TCTRL_TIE_MASK;

	/* Enable Interrupts */
	NVIC_SetPriority(PIT_IRQn, 128); // 0, 64, 128 or 192
	NVIC_ClearPendingIRQ(PIT_IRQn); 
	NVIC_EnableIRQ(PIT_IRQn);	
}


void Start_PIT1(void) {
// Enable counter
	PIT->CHANNEL[0].TCTRL |= PIT_TCTRL_TEN_MASK;
}

void Stop_PIT1(void) {
// disable counter
	PIT->CHANNEL[0].TCTRL &= ~PIT_TCTRL_TEN_MASK;
}
void Start_PIT2(void) {
// Enable counter
	PIT->CHANNEL[0].TCTRL |= PIT_TCTRL_TEN_MASK;
}

void Stop_PIT2(void) {
// disable counter
	PIT->CHANNEL[0].TCTRL &= ~PIT_TCTRL_TEN_MASK;
}


void PIT_IRQHandler() {
	static unsigned LCD_update_delay = LCD_UPDATE_PERIOD;

	//clear pending IRQ
	NVIC_ClearPendingIRQ(PIT_IRQn);
	
	// check to see which channel triggered interrupt 
	if (PIT->CHANNEL[0].TFLG & PIT_TFLG_TIF_MASK) {
		// clear status flag for timer channel 0
		PIT->CHANNEL[0].TFLG &= PIT_TFLG_TIF_MASK;

		PIN_TRIG_PT->PCOR |= PIN_TRIG;	
				
	} else if (PIT->CHANNEL[1].TFLG & PIT_TFLG_TIF_MASK) {
		// clear status flag for timer channel 1
		PIT->CHANNEL[1].TFLG &= PIT_TFLG_TIF_MASK;
		timeoutFlag = 1;
		
	} 
}

// ================================================================================
// TPM IRQ and init here:

// ================================================================================
volatile int is_rising = 0;

#if 0
void TPM0_IRQHandler()
{
	NVIC_ClearPendingIRQ(TPM0_IRQn);
	
	TPM0->SC &= ~TPM_SC_CMOD_MASK;
	
	if (is_rising)
	{
		Control_RGB_LEDs(1, 0, 0);
		Delay(10);
		is_rising = 0;
	}
	else
	{
		Control_RGB_LEDs(0, 1, 0);
		Delay(10);
		is_rising = 1;
	}
	
	TPM0->CNT = 0;
	TPM0->CONTROLS[0].CnSC |= TPM_CnSC_CHF_MASK;
	TPM0->STATUS = 0;
	
	
	TPM0->SC |=  TPM_SC_CMOD(1);
}
#else

void TPM0_IRQHandler(void) {
		
	//Keep track of overflow for time elapsed
	if(TPM0->STATUS & TPM_STATUS_TOF_MASK){
		
		//clear timer overflow flag and increment overflow counter
		TPM0->SC |= TPM_SC_TOF_MASK;
		overflow++;
	}
	
	//Rising edge or falling edge has occured, measurement has either started or completed
	if(TPM0->STATUS & TPM_STATUS_CH0F_MASK) {
		
		//Clear channel flag
		TPM0->CONTROLS[0].CnSC |= TPM_CnSC_CHF_MASK;
		
		//When it's rising edge start measuring
		if(!echoFallingEdge) {
			Stop_PIT2();
						
			//Signal to start measurement
			echoFallingEdge = 1;
			TPM0->CNT = 0;
		}
		
		//When it's falling edge capture time elapsed
		else {
			// Get the time elapsed
			ticksElapsed = TPM0->CONTROLS[0].CnV + overflow*PWM_MAX_COUNT;
			
			//Set the flag that the measurement is complete
			measureFlag = 1;
			
			echoFallingEdge = 0;
			
			//Disable TPM
			TPM0->SC &= ~(TPM_SC_CMOD_MASK);
		}
	}
}
#endif

void Init_TPM()
{
	//turn on clock to TPM 
	SIM->SCGC6 |= SIM_SCGC6_TPM0_MASK; // | SIM_SCGC6_TPM2_MASK;
	
	//set clock source for tpm
	SIM->SOPT2 |= (SIM_SOPT2_TPMSRC(1) | SIM_SOPT2_PLLFLLSEL_MASK);

	//load the counter and mod
	TPM0->MOD = PWM_MAX_COUNT;
	
	// This may be optional for input capture mode
	TPM0->CNT = 0;		
	
	TPM0->CONTROLS[0].CnSC = 0;
	TPM0->CONTROLS[0].CnSC = TPM_CnSC_CHIE_MASK | TPM_CnSC_ELSB_MASK | TPM_CnSC_ELSA_MASK;
	
	TPM0->CONF |= TPM_CONF_CROT_MASK;
	//TPM0->CONF |= TPM_CONF_CSOT_MASK;
	
	TPM0->SC = (TPM_SC_CMOD(1) | TPM_SC_PS(3));
	
	// This may be optional for input capture mode
	TPM0->CONTROLS[0].CnV = 0;	
	
	NVIC_SetPriority(TPM0_IRQn, 128);
	NVIC_ClearPendingIRQ(TPM0_IRQn);
	NVIC_EnableIRQ(TPM0_IRQn);
}

// ================================================================================

void Set_PWM_Value(uint8_t duty_cycle) {
	uint16_t n;
	
	n = duty_cycle*PWM_MAX_COUNT/100; 
	TPM0->CONTROLS[1].CnV = n;
	TPM2->CONTROLS[1].CnV = n;
	TPM0->CONTROLS[0].CnV = n;
}
// *******************************ARM University Program Copyright � ARM Ltd 2013*************************************   
