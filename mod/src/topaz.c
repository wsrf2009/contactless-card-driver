










#include <linux/string.h>

#include "common.h"
#include "topaz.h"
#include "crc.h"
#include "pn512app.h"
#include "pn512.h"
#include "delay.h"
#include "picc.h"
#include "typeA.h"
#include "debug.h"




static UINT8 TopazPcdHandler(UINT8 *cmdBuf, UINT8 cmdLen, UINT8 *resBuf, UINT8 *resLen, UINT16 timeout)
{
    UINT8 i;
    UINT8 ret;
    UINT8 nbytes;
    UINT8 crcL;
    UINT8 crcH;


    PrtMsg(DBGL5, "%s: start, cmdLen = %02X\n", __FUNCTION__, cmdLen);    

    SetRegBit(REG_MANUALRCV, BIT_PARITYDISABLE);        //Parity disable
    ComputeCrc(CRC_B, cmdBuf, cmdLen, cmdBuf + cmdLen, cmdBuf + cmdLen + 1);
    cmdLen += 2; 
    PrtMsg(DBGL5, "CRC1 = %02X, CRC2 = %02X\n", cmdBuf[cmdLen - 2], cmdBuf[cmdLen - 1]);
    RegWrite(REG_BITFRAMING, 0x07);        // Set TxLastBits to 7
    FIFOFlush();
    RegWrite(REG_FIFODATA, cmdBuf[0]);
    SetTimer100us(100);                      // 100 us Time out
    PcdHandlerCmd(CMD_TRANSMIT, BIT_PARITYERR); 
    Delay1us(100);
    RegWrite(REG_BITFRAMING, 0x00);        // Set TxLastBits to 0
    for(i = 1; i < cmdLen; i++)
    {
        RegWrite(REG_FIFODATA, cmdBuf[i]);
        SetTimer100us(100);                  // 1 ms Time out
        PcdHandlerCmd(CMD_TRANSMIT, BIT_PARITYERR);
        Delay1us(100);
    }

    ClearRegBit(REG_MANUALRCV, BIT_PARITYDISABLE);      // Parity enable
    SetTimer100us(timeout * 10);                // Time out

  PrtMsg(DBGL5, "%s: REG_COMMIRQ = %02X\n", __FUNCTION__, RegRead(REG_COMMIRQ));

    RegWrite(REG_COMMIRQ, 0x7F);           //Clear the Rx Irq bit first
    PrtMsg(DBGL5, "%s: REG_COMMIRQ = %02X\n", __FUNCTION__, RegRead(REG_COMMIRQ));
	
    RegWrite(REG_COMMAND, CMD_RECEIVE);
    SetRegBit(REG_CONTROL, BIT_TSTARTNOW);
    i = 0;
    while(!(RegRead(REG_COMMIRQ) & 0x21))
    {
        nbytes = RegRead(REG_FIFOLEVEL);
        PrtMsg(DBGL5, "%s: REG_FIFOLEVEL = %02X\n", __FUNCTION__, nbytes);
        PrtMsg(DBGL5, "%s: REG_RXMODE = %02X\n", __FUNCTION__, RegRead(REG_RXMODE));
        if(nbytes > 1)
        {
            FIFORead(resBuf + i, nbytes);
            i += nbytes;
        }
    }
    PrtMsg(DBGL5, "%s: REG_COMMIRQ = %02X\n", __FUNCTION__, RegRead(REG_COMMIRQ));
	PrtMsg(DBGL5, "%s: REG_FIFOLEVEL = %02X\n", __FUNCTION__, RegRead(REG_FIFOLEVEL));
    PrtMsg(DBGL5, "%s: REG_RXMODE = %02X\n", __FUNCTION__, RegRead(REG_RXMODE));

    SetRegBit(REG_CONTROL, BIT_TSTOPNOW);
//    nbytes = GetBitNumbersReceived();
    nbytes = RegRead(REG_FIFOLEVEL);
    PrtMsg(DBGL5, "%s: nbytes = %02X\n", __FUNCTION__, nbytes);

    FIFORead((resBuf + i), nbytes);
//    for(i = 0; i < 8; i++)
//    {
//        PrtMsg(DBGL5, "%02X\n", resBuf[i]);
//    }
    i += nbytes;

    PrtMsg(DBGL5, "%s: i = %02X\n", __FUNCTION__, i);

     // Response   Processing   
    if(i >= 4)
    {
        // Read the data from card to buffer       
        ComputeCrc(CRC_B, resBuf, (i - 2), &crcL, &crcH);
        if((crcL == resBuf[i - 2]) || (crcH == resBuf[i - 1]))
        {
            *resLen = i - 2;
            ret = ERROR_NO;
        }
        else
        {
            *resLen  = 0;
            ret = ERROR_CRC;
        }
    }
    else
    {
        ret = ERROR_BYTECOUNT;
    } 

    PrtMsg(DBGL5, "%s: exit, ret = %02X\n", __FUNCTION__, ret);
    
    return(ret);
}


/*****************************************************************/
//      Topaz RID Command
/*****************************************************************/
static UINT8 TopazRID(void)       
{
    UINT8 ret = ERROR_NO;
    UINT8 nbytes;
    UINT8 tempBuf[9];


    PrtMsg(DBGL5, "%s: start\n", __FUNCTION__);    
    
    //************* Initializing ******************************//
    ClearRegBit(REG_TXMODE, BIT_TXCRCEN);         // Disable TxCRC
    ClearRegBit(REG_RXMODE, BIT_RXCRCEN);         // Disable RxCRC
    ClearRegBit(REG_STATUS2, BIT_MFCRYPTO1ON);    // Disable crypto 1 unit

    //************** Anticollision Loop ***************************//
    memcpy(tempBuf, "\x78\x00\x00\x00\x00\x00\x00", 7);
    ret = TopazPcdHandler(tempBuf, 7, tempBuf, &nbytes, 9);
    if(ret == ERROR_NO)
    {
        picc.snLen = 4;
        memcpy(picc.sn, tempBuf + 2, 4);
        PrtMsg(DBGL5, "%s: TOPAZ had been found\n", __FUNCTION__);
        PrtMsg(DBGL5, "sn: %02X, %02X, %02X, %02X\n", tempBuf[2],  tempBuf[3], tempBuf[4], tempBuf[5]);
    }
    
    return(ret);  
}



/*****************************************************************/
void PollTopazTags(void)
{
    PrtMsg(DBGL1, "%s: start\n", __FUNCTION__);

    // reset speed settings to 106Kbps
    PcdConfigIso14443Type(CONFIGTYPEA, TYPEA_106TX);
    PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_106RX);
    
    // check for any card in the field
    if(PcdRequestA(PICC_WUPA, picc.ATQA) == ERROR_NOTAG)
    {
        picc.type = PICC_ABSENT;
    }
    else
    {
        PrtMsg(DBGL5, "%s: ATQA: %02X, %02X\n", __FUNCTION__, picc.ATQA[0], picc.ATQA[1]);
        if(TopazRID() == ERROR_NO)
        {
            picc.type = PICC_TOPAZ;
        }
    }
}


/*****************************************************************/
UINT8 TopazXfrHandle(UINT8 *cmdBuf, UINT16 cmdLen, UINT8 *resBuf, UINT16 *resLen)
{
    UINT8 ret = SLOT_NO_ERROR;
    UINT16  timeOut;
    UINT8 tempLen;
    

    if(cmdBuf[0] == TOPAZ_RID)
    {
        memset(cmdBuf + 1, 0x00, 6);
        timeOut = 8;
        cmdLen = 7;
    }
    else if(cmdBuf[0] == TOPAZ_RALL)
    {
        cmdBuf[1] = 0x00;
        cmdBuf[2] = 0x00;
        timeOut = 8;
        memcpy(cmdBuf + 3, picc.sn, 4);
    }
    else if(cmdBuf[0] == TOPAZ_READ)
    {
        cmdBuf[2] = 0x00;
        timeOut = 8;
        memcpy(cmdBuf + 3, picc.sn, 4);
    }
    else if(cmdBuf[0] == TOPAZ_WRITE_E)
    {
        timeOut = 70;
        memcpy(cmdBuf + 3, picc.sn, 4);
    }
    else if(cmdBuf[0] == TOPAZ_WRITE_NE)
    {
        timeOut = 40;
        memcpy(cmdBuf + 3, picc.sn, 4);
    }
    else
    {
        timeOut = 8;
    }
    cmdLen = 7;
    ret = TopazPcdHandler(cmdBuf, (UINT8)cmdLen, resBuf, &tempLen, timeOut);
    if(ret == ERROR_NO)
    {
        *resLen = tempLen;
        ret = SLOT_NO_ERROR;
    }
    else
    {
        resBuf[0] = 0x64;
        resBuf[1] = 0x01;
        *resLen = 2;
        ret = SLOTERROR_ICC_MUTE;
    }
    
    return(ret);
}


UINT8 TopazTransmissionHandle(UINT8 *cmdBuf, UINT16 cmdLen, UINT8 *resBuf, UINT16 *resLen)
{
    UINT8  ret = SLOT_NO_ERROR;
    UINT16 timeout;
    UINT8  tempLen;
    

    if(cmdBuf[0] == TOPAZ_RID)
    {
        memset(cmdBuf + 1, 0x00, 6);
        timeout = 8;
        cmdLen  = 7;
    }
    else if(cmdBuf[0] == TOPAZ_RALL)
    {
        cmdBuf[1] = 0x00;
        cmdBuf[2] = 0x00;
        timeout   = 8;
        memcpy(cmdBuf + 3, picc.sn, 4);
    }
    else if(cmdBuf[0] == TOPAZ_READ)
    {
        cmdBuf[2] = 0x00;
        timeout   = 8;
        memcpy(cmdBuf + 3, picc.sn, 4);
    }
    else if(cmdBuf[0] == TOPAZ_WRITE_E)
    {
        timeout = 70;
        memcpy(cmdBuf + 3, picc.sn, 4);
    }
    else if(cmdBuf[0] == TOPAZ_WRITE_NE)
    {
        timeout = 40;
        memcpy(cmdBuf + 3, picc.sn, 4);
    }
    else
    {
        timeout = 8;
    }
    cmdLen = 7;
    ret = TopazPcdHandler(cmdBuf, (UINT8)cmdLen, resBuf, &tempLen, timeout);
    if(ret == ERROR_NO)
    {
        *resLen = tempLen;
        ret     = SLOT_NO_ERROR;
    }
    else
    {
        resBuf[0] = 0x64;
        resBuf[1] = 0x01;
        *resLen   = 2;
        ret       = SLOTERROR_ICC_MUTE;
    }
    
    return(ret);
}

