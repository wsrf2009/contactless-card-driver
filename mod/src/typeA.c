

//****************************************************
// Name: ISO 14443 Type A source file
// Date: 2012/12/05
// Author: Alex Wang
// Version: 1.0
//****************************************************



#include <linux/string.h>


#include "common.h"
#include "typeA.h"
#include "picc.h"
#include "pn512app.h"
#include "pn512.h"
#include "delay.h"
#include "part4.h"
#include "debug.h"



const UINT8 selectCmd[3] = {PICC_SELVL1, PICC_SELVL2, PICC_SELVL3};





/****************************************************************/
//       Type A PICC halt
/****************************************************************/
void PiccHaltA(void)
{
    UINT8 ret;


    PrtMsg(DBGL4, "%s: start\n", __FUNCTION__);

    if (picc.states != PICC_POWEROFF)
    {
        SetRegBit(REG_TXMODE, BIT_TXCRCEN);    // TXCRC enable
        SetRegBit(REG_RXMODE, BIT_RXCRCEN);    // RXCRC enable

        // ************* Cmd Sequence **********************************
        FIFOFlush();
        // Issue Command Sequence
        RegWrite(REG_FIFODATA, PICC_HALT);    // Halt command code
        RegWrite(REG_FIFODATA, 0x00);         // dummy address
        SetTimer100us(10);                    // 1 ms Time out
        ret = PcdHandlerCmd(CMD_TRANSCEIVE, BIT_PARITYERR | BIT_CRCERR); 
        picc.states = PICC_IDLE;
    }

    PrtMsg(DBGL4, "%s: exit\n", __FUNCTION__);

}



/*****************************************************************/
//       Type A anticollision
/*****************************************************************/
static UINT8 PiccCascAnticoll (UINT8 selCode, UINT8 *uid)       
{
    UINT8 ret = ERROR_NO;
    UINT8 nbytes;
    UINT8 nbits;
    UINT8 i;
    UINT8 nBytesReceived;
    UINT8 byteOffset;
    UINT8 uidCRC;
    UINT8 bcnt = 0;  
    UINT8 recBuf[5];


    PrtMsg(DBGL3, "%s: Start\n", __FUNCTION__);    

    //************* Initializing ******************************//
    ClearRegBit(REG_TXMODE, BIT_TXCRCEN);            // Disable TxCRC
    ClearRegBit(REG_RXMODE, BIT_RXCRCEN);            // Disable RxCRC
    RegWrite(REG_COLL, 0x00);                        // ValuesAfterColl = 0
    
    //************** Anticollision Loop ***************************//
    while(ret == ERROR_NO)
    {
        nbits = bcnt & 0x07;                         // remaining number of bits
        if(nbits) 
        {
            nbytes = (bcnt / 8) + 1;   
        } 
        else 
        {
            nbytes = bcnt / 8;
        }
        RegWrite(REG_BITFRAMING, (nbits << 4) | nbits);    // TxLastBits/RxAlign on nb_bi

        FIFOFlush();
        RegWrite(REG_FIFODATA, selCode);                             // SEL: select code
        RegWrite(REG_FIFODATA, 0x20 + ((bcnt / 8) << 4) + nbits);   //NVB: number of bytes send
        for(i = 0; i < nbytes; i++) 
        { 
            RegWrite(REG_FIFODATA, uid[i]);                          // UID: 0~40 data bits
        }
        
        SetTimer100us(100);                             // 1 ms Time out
        ret = PcdHandlerCmd(CMD_TRANSCEIVE, BIT_PARITYERR | BIT_COLLERR); 
    
        if(ret == ERROR_NO|| ret == ERROR_COLL)        // no other occurred
        {   
            // Response   Processing   
            nBytesReceived = RegRead(REG_FIFOLEVEL);
            i = GetBitNumbersReceived();
            bcnt += i - nbits;
            
            // i= no. of bits received
            if(bcnt > 40) 
            {
                ret = ERROR_BITCOUNT;
            } 
            else 
            {
                for(i = 0; i < nBytesReceived; i++) 
                {
                    // Read the data from card to buffer
                    recBuf[i] = RegRead(REG_FIFODATA);
                }
                byteOffset = 0;
                if(nbits != 0)            // last byte was not complete
                {        
                    uid[nbytes - 1] |= recBuf[0];
                    byteOffset = 1;
                }
                for(i = 0; i < (4 - nbytes); i++) 
                {
                   uid[nbytes + i] = recBuf[i + byteOffset];
                }
  
                if(ret == ERROR_NO)       // no error and no collision
                { 
                    // bcc check
                    uidCRC = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
                    if(uidCRC != recBuf[nBytesReceived - 1]) 
                    {
                        ret = ERROR_SERNR;
                    }
                    break;
                } 
                else      // collision occurred
                {                  
                    ret = ERROR_NO;
                }
            }
        }
    }
    if (ret != ERROR_NO) 
    {                                                  
        memcpy(uid, "\x00\x00\x00\x00", 4);
    }
    
    //------------Adjustments from initialization reset-------------
    RegWrite(REG_BITFRAMING, 0x00);        // TxLastBits/RxAlign 0
    RegWrite(REG_COLL, 0x80);              // ValuesAfterColl = 1

    PrtMsg(DBGL3, "%s: exit\n", __FUNCTION__);

    return(ret);
}


/*****************************************************************/
//       Type A Select
/*****************************************************************/
UINT8 PiccCascSelect(UINT8 selCode, UINT8 *uid, UINT8 *sak)
{
    UINT8 ret = ERROR_NO;
    UINT8 i;
    UINT8 j;


    PrtMsg(DBGL3, "%s: start, selCode = %02X\n", __FUNCTION__, selCode);    

    SetRegBit(REG_TXMODE, BIT_TXCRCEN);     // TXCRC enable
    SetRegBit(REG_RXMODE, BIT_RXCRCEN);     // RXCRC enable

    //************* Cmd Sequence **********************************//
    FIFOFlush();
    RegWrite(REG_FIFODATA, selCode);    // SEL: select code
    RegWrite(REG_FIFODATA, 0x70);       // NVB: number of bytes send
    for(i = 0, j = 0; i < 4; i++) 
    {
        RegWrite(REG_FIFODATA, uid[i]);
        j ^= uid[i];
    }
    RegWrite(REG_FIFODATA, j);
    SetTimer100us(100);                      // 1 ms Time out
    ret = PcdHandlerCmd(CMD_TRANSCEIVE, BIT_PARITYERR | BIT_CRCERR); 

    *sak = 0;
    if(ret == ERROR_NO)         // No timeout occured
    {   
        i = GetBitNumbersReceived();

        // i= no. of bits received
        if(i != 8)             // last byte is not complete
        {  
            ret = ERROR_BITCOUNT;
        }
        else 
        {
            *sak = RegRead(REG_FIFODATA);
            PrtMsg(DBGL1, "%s: SAK = %02X\n", __FUNCTION__, *sak);
        }
    }

  PrtMsg(DBGL3, "%s: exit\n", __FUNCTION__);

    return(ret);
}


/****************************************************************/
//       Send REQA/WUPA command to polling PICC
/****************************************************************/
UINT8 PcdRequestA(UINT8 reqCmd, UINT8 *pATQ)
{
    UINT8 ret = ERROR_NO;
    unsigned int i;


    PrtMsg(DBGL3, "%s: Start\n", __FUNCTION__);    

    //************* initialize ******************************//
    ClearRegBit(REG_MODE, BIT_DETECTSYNC);             // disable DetectSync if activated before 
    ClearRegBit(REG_TXMODE, BIT_TXCRCEN);              // Disable TxCRC
    ClearRegBit(REG_RXMODE, BIT_RXCRCEN);              // Disable RxCRC
    ClearRegBit(REG_STATUS2, BIT_MFCRYPTO1ON);         // Disable crypto 1 unit
    RegWrite(REG_BITFRAMING, 0x07);                    // Set TxLastBits to 7   

    FIFOFlush();                           // empty FIFO
    RegWrite(REG_FIFODATA, reqCmd);
    SetTimer100us(100);                     // 1 ms Time out
    ret = PcdHandlerCmd(CMD_TRANSCEIVE, BIT_PARITYERR | BIT_COLLERR);
    if(ret) 
    {       
        // error occur
        pATQ[0] = 0x00;
        pATQ[1] = 0x00;
        PrtMsg(DBGL1, "%s: fail to get ATQA\n", __FUNCTION__);
    }
    else 
    {
        i = GetBitNumbersReceived();
        
        // i= no. of bits received
        if(i != 16) 
        {
            ret = ERROR_BITCOUNT;
            pATQ[0] = 0x00;
            pATQ[1] = 0x00;

        } 
        else 
        {
            pATQ[0] = RegRead(REG_FIFODATA);
            pATQ[1] = RegRead(REG_FIFODATA);
            ret = ERROR_NO;
            picc.states = PICC_READY;
        }
    }

    
    RegWrite(REG_BITFRAMING, 0x00);            // Reset TxLastBits to 0

    PrtMsg(DBGL3, "%s: exit\n", __FUNCTION__);  
	
    return(ret); 
}




UINT8 TypeASelect(void)
{
    UINT8 level=0;
    UINT8 cardUID[5];


    PrtMsg(DBGL3, "%s: start\n", __FUNCTION__);    

    // reset speed settings to 106Kbps
    PcdConfigIso14443Type(CONFIGTYPEA, TYPEA_106TX);
    PcdConfigIso14443Type(CONFIGNOTHING, TYPEA_106RX); 

    if(PcdRequestA(PICC_WUPA, picc.ATQA) == ERROR_NOTAG)
    {
        Delay1us(300);
        if(PcdRequestA(PICC_WUPA, picc.ATQA) == ERROR_NOTAG)
        {
            PrtMsg(DBGL3, "%s: exit, no card had been found\n", __FUNCTION__);   
            return(ERROR_NOTAG);
        }
    }
    
    do
    {
        Delay1us(100);
        if(PiccCascAnticoll(selectCmd[level], cardUID) != ERROR_NO) 
        {
            PiccHaltA();
            break;
        }
        Delay1us(100);
        if(PiccCascSelect(selectCmd[level], cardUID, &(picc.SAK)) != ERROR_NO) 
        {
            break;
        }

        if(level == 0) 
        {
            // Cascade Level 1
            if(cardUID[0] == 0x88)    // uid0 = 0x88, CT present and next cascade level will be implement
            {
                memcpy(picc.sn, cardUID + 1, 3);
                picc.snLen = 3;
            } 
            else 
            {
                memcpy(picc.sn, cardUID, 4);
                picc.snLen = 4;
                level |= 0x80;            // quit the  loop
            }
        } 
        else if(level == 1) 
        {
            // Cascade Level 2
            if(cardUID[0] == 0x88)    // uid3 = 0x88, CT present and next cascade level will be implement
            {
                memcpy(picc.sn + 3, cardUID + 1,3);
                picc.snLen = 6;
            } 
            else 
            {
                memcpy(picc.sn + 3, cardUID, 4);
                picc.snLen = 7;
                level |= 0x80;            // quit the loop
            }
        } 
        else 
        {
            // Cascade Level 3
            memcpy(picc.sn + 6, cardUID, 4);
            picc.snLen = 10;
            level |= 0x80;               // quit the  loop
        }
        
        level++;                         // next level code 
    } while(level < 0x80); 
    
    if(level & 0x80)
    {
        PrtMsg(DBGL3, "***********************\n");
        PrtMsg(DBGL1, "%s: has found a tag\n", __FUNCTION__);
        PrtMsg(DBGL1, "%s: sn:", __FUNCTION__);
        for(level = 0; level < picc.snLen; level++)
        {
            PrtMsg(DBGL1, " %02X", picc.sn[level]);
        }
        PrtMsg(DBGL1, "\n");
        return(ERROR_NO);
    }
    else
    {
        PrtMsg(DBGL3, "%s: No card be found\n", __FUNCTION__);
        return(ERROR_NOTAG);
    }
}



void PollTypeATags(void)
{
    UINT8 ret;
    

    PrtMsg(DBGL3, "%s: start\n", __FUNCTION__);

    // check for any card in the field
    ret = TypeASelect();

    if(ret == ERROR_NO)
    {
        // Check the SAK
        if(picc.SAK & 0x20)
        {
            // picc compliant with ISO/IEC 14443-4
            picc.CID = GetCID(picc.sn);
            if((BITISSET(pcd.fgPoll, BIT_AUTORATS)))
            {
                // auto ATS
                Delay1us(300);
                if(PcdRequestATS() == ERROR_NO)
                {
                    picc.type = PICC_TYPEA_TCL;        // typeaA PICC which compliant to ISO/IEC 14443-4 
                    PiccPPSCheckAndSend();
                    picc.FSC = FSCConvertTbl[picc.FSCI] - 3;      // FSC excluding EDC and PCB, refer to Figure14 --- Block format
                    if(BITISSET(picc.fgTCL, BIT_CIDPRESENT))
                    {
                        picc.FSC--;                              // FSC excluding CID, refer to Figure14 --- Block format
                    }
                }
                else
                {
                    if(DeselectRequest() != ERROR_NO)
                    {
                        PiccHaltA();
                    }
                
                    picc.type = PICC_ABSENT;
                }
            }
        }
        else
        {
            picc.type = PICC_MIFARE;
        }

    }
    else
    {
        picc.type = PICC_ABSENT;
    }

    PrtMsg(DBGL3, "%s: exit\n", __FUNCTION__);
}





