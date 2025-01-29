/*
  Copyright (c) 2014 Arduino.  All right reserved.

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

//#include "Arduino.h"
#include "SERCOM.h"
//#include "variant.h"

#ifndef WIRE_RISE_TIME_NANOSECONDS
// Default rise time in nanoseconds, based on 4.7K ohm pull up resistors
// you can override this value in your variant if needed
#define WIRE_RISE_TIME_NANOSECONDS 125
#endif

SERCOM::SERCOM(Sercom* s)
{
  sercom = s;
}

/* 	=========================
 *	===== Sercom UART
 *	=========================
*/
void SERCOM::initUART(SercomUartMode mode, SercomUartSampleRate sampleRate, uint32_t baudrate)
{
  initClockNVIC();
  resetUART();

  //Setting the CTRLA register
  sercom->USART.CTRLA.reg =	SERCOM_USART_CTRLA_MODE(mode);

  //Setting the Interrupt register
  sercom->USART.INTENSET.reg =	SERCOM_USART_INTENSET_RXC;  //Received complete

  if ( mode == UART_INT_CLOCK ) {
      uint64_t ratio = 1048576;
      ratio *= baudrate;
      ratio /= SystemCoreClock;
      sercom->USART.BAUD.reg = ( uint16_t )( 65536 - ratio );
  }
}
void SERCOM::initFrame(SercomUartCharSize charSize, SercomDataOrder dataOrder, SercomParityMode parityMode, SercomNumberStopBit nbStopBits)
{
  //Setting the CTRLA register
  sercom->USART.CTRLA.reg |=	SERCOM_USART_CTRLA_FORM( (parityMode == SERCOM_NO_PARITY ? 0 : 1) ) |
                dataOrder << SERCOM_USART_CTRLA_DORD_Pos;

  //Setting the CTRLB register
  sercom->USART.CTRLB.reg |=	SERCOM_USART_CTRLB_CHSIZE(charSize) |
                nbStopBits << SERCOM_USART_CTRLB_SBMODE_Pos |
                (parityMode == SERCOM_NO_PARITY ? 0 : parityMode) << SERCOM_USART_CTRLB_PMODE_Pos; //If no parity use default value
}

void SERCOM::initPads(SercomUartTXPad txPad, SercomRXPad rxPad)
{
  //Setting the CTRLA register
  sercom->USART.CTRLA.reg |=	SERCOM_USART_CTRLA_TXPO |
                SERCOM_USART_CTRLA_RXPO(rxPad);

// wait
  while(sercom->USART.STATUS.bit.SYNCBUSY);

  // Enable Transceiver and Receiver
  sercom->USART.CTRLB.reg |= SERCOM_USART_CTRLB_TXEN | SERCOM_USART_CTRLB_RXEN ;
}

void SERCOM::resetUART()
{
// wait
  while(sercom->USART.STATUS.bit.SYNCBUSY);

  // Start the Software Reset
  sercom->USART.CTRLA.bit.SWRST = 1 ;

  while ( sercom->USART.CTRLA.bit.SWRST)
  {
    // Wait for both bits Software Reset from CTRLA coming back to 0
  }
}

void SERCOM::enableUART()
{
  //Wait for then enable bit from SYNCBUSY is equal to 0;
  while(sercom->USART.STATUS.bit.SYNCBUSY);

  //Setting  the enable bit to 1
  sercom->USART.CTRLA.bit.ENABLE = 0x1u;
}

void SERCOM::flushUART()
{
  // Skip checking transmission completion if data register is empty
  // Wait for transmission to complete, if ok to do so.
  while(!sercom->USART.INTFLAG.bit.TXC && onFlushWaitUartTXC);

  onFlushWaitUartTXC = false;
}

void SERCOM::clearStatusUART()
{
  //Reset (with 0) the STATUS register
  sercom->USART.STATUS.reg = SERCOM_USART_STATUS_RESETVALUE;
}

bool SERCOM::availableDataUART()
{
  //RXC : Receive Complete
  return sercom->USART.INTFLAG.bit.RXC;
}

bool SERCOM::isUARTError()
{
  return sercom->USART.STATUS.reg &
               ( SERCOM_USART_STATUS_BUFOVF | SERCOM_USART_STATUS_FERR |
                 SERCOM_USART_STATUS_PERR );
}

void SERCOM::acknowledgeUARTError()
{
  if( isBufferOverflowErrorUART() )
      sercom->USART.STATUS.reg |= SERCOM_USART_STATUS_BUFOVF;
  if( isFrameErrorUART() )
      sercom->USART.STATUS.reg |= SERCOM_USART_STATUS_FERR;
  if( isParityErrorUART() )
      sercom->USART.STATUS.reg |= SERCOM_USART_STATUS_PERR;
}

bool SERCOM::isBufferOverflowErrorUART()
{
  //BUFOVF : Buffer Overflow
  return sercom->USART.STATUS.bit.BUFOVF;
}

bool SERCOM::isFrameErrorUART()
{
  //FERR : Frame Error
  return sercom->USART.STATUS.bit.FERR;
}

void SERCOM::clearFrameErrorUART()
{
  // clear FERR bit writing 1 status bit
  sercom->USART.STATUS.bit.FERR = 1;
}

bool SERCOM::isParityErrorUART()
{
  //PERR : Parity Error
  return sercom->USART.STATUS.bit.PERR;
}

bool SERCOM::isDataRegisterEmptyUART()
{
  //DRE : Data Register Empty
  return sercom->USART.INTFLAG.bit.DRE;
}

uint8_t SERCOM::readDataUART()
{
  return sercom->USART.DATA.bit.DATA;
}

int SERCOM::writeDataUART(uint8_t data)
{
  // Wait for data register to be empty
  while(!isDataRegisterEmptyUART());

  //Put data into DATA register
  sercom->USART.DATA.reg = (uint16_t)data;

  // indicate it's ok to wait for TXC flag when flushing
  onFlushWaitUartTXC = true;

  return 1;
}

void SERCOM::enableDataRegisterEmptyInterruptUART()
{
  sercom->USART.INTENSET.reg = SERCOM_USART_INTENSET_DRE;
}

void SERCOM::disableDataRegisterEmptyInterruptUART()
{
  sercom->USART.INTENCLR.reg = SERCOM_USART_INTENCLR_DRE;
}


void SERCOM::initClockNVIC( void )
{
   
    uint32_t id = GCLK_CLKCTRL_ID_SERCOM0_CORE_Val;
    uint32_t apbMask = PM_APBCMASK_SERCOM0;
    uint32_t irqn = SERCOM0_IRQn;
    if( sercom == SERCOM1 ) {
        id = GCLK_CLKCTRL_ID_SERCOM1_CORE_Val;
        apbMask = PM_APBCMASK_SERCOM1;
        irqn = SERCOM1_IRQn;
    }
    if( sercom == SERCOM2 ) {
        id = GCLK_CLKCTRL_ID_SERCOM2_CORE_Val;
        apbMask = PM_APBCMASK_SERCOM2;
        irqn = SERCOM2_IRQn;
    }
    if( sercom == SERCOM3 ) {
        id = GCLK_CLKCTRL_ID_SERCOM3_CORE_Val;
        apbMask = PM_APBCMASK_SERCOM3;
        irqn = SERCOM3_IRQn;
    }
    if( sercom == SERCOM4 ) {
        id = GCLK_CLKCTRL_ID_SERCOM4_CORE_Val;
        apbMask = PM_APBCMASK_SERCOM4;
        irqn = SERCOM4_IRQn;
    }

    if( sercom == SERCOM5 ) {
        id = GCLK_CLKCTRL_ID_SERCOM5_CORE_Val;
        apbMask = PM_APBCMASK_SERCOM5;
        irqn = SERCOM5_IRQn;
    }

    // Ensure that PORT is enabled
    enableAPBBClk( PM_APBBMASK_PORT, 1 );
    initGenericClk( GCLK_CLKCTRL_GEN_GCLK0_Val, id );
    enableAPBCClk( apbMask, 1 );
    NVIC_EnableIRQ( (IRQn_Type)irqn );  
}


void SERCOM::enableAPBBClk( uint32_t item, uint8_t enable )
{
    item &= PM_APBBMASK_MASK;
    if( enable )
        PM->APBBMASK.reg |= item;
    else
        PM->APBBMASK.reg &= ~( item );
}

void SERCOM::enableAPBCClk( uint32_t item, uint8_t enable )
{
    item &= PM_APBCMASK_MASK;
    if( enable )
        PM->APBCMASK.reg |= item;
    else
        PM->APBCMASK.reg &= ~( item );
}


int8_t SERCOM::initGenericClk( uint32_t genClk, uint32_t id )
{
	if( genClk > GCLK_CLKCTRL_GEN_GCLK7_Val ) return -1;
	if( id > GCLK_CLKCTRL_ID_PTC_Val ) return -1;
	
	while (sercom->I2CM.STATUS.bit.SYNCBUSY){}

	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID( id ) | GCLK_CLKCTRL_GEN( genClk ) |
	GCLK_CLKCTRL_CLKEN;
	

	return 0;
}

