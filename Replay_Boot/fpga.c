//
// Support routines for the FPGA including file transfer, system control and DRAM test
//

#include "fpga.h"
#include "fileio.h"
#include "hardware.h"
#include "osd.h"  // for keyboard input to DRAM debug only
#include "config.h"
#include "messaging.h"

// Bah! But that's how it is proposed by this lib...
#include "tinfl.c"
// ok, so it is :-)
#include "loader.c"

const uint8_t kMemtest[128] =
{
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,
    0xAA,0xAA,0x55,0x55,0x55,0x55,0xAA,0xAA,0xAA,0x55,0x55,0xAA,0x55,0xAA,0xAA,0x55,
    0x00,0x01,0x00,0x02,0x00,0x04,0x00,0x08,0x00,0x10,0x00,0x20,0x00,0x40,0x00,0x80,
    0x01,0x00,0x02,0x00,0x04,0x00,0x08,0x00,0x10,0x00,0x20,0x00,0x40,0x00,0x80,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x0E,0xEE,0xEE,0x44,0x4D,
    0x11,0x11,0xEE,0xEC,0xAA,0x55,0x55,0xAB,0xBB,0xBB,0x00,0x0A,0xFF,0xFF,0x80,0x09,
    0x80,0x08,0x00,0x08,0xAA,0xAA,0x55,0x57,0x55,0x55,0xCC,0x36,0x33,0xCC,0xAA,0xA5,
    0xAA,0xAA,0x55,0x54,0x00,0x00,0xFF,0xF3,0xFF,0xFF,0x00,0x02,0x00,0x00,0xFF,0xF1
};

//
// General
//

uint8_t FPGA_Default(void) // embedded in FW, something to start with
{
  uint32_t secCount;
  unsigned long time;

  uint8_t dBuf1[8192];
  uint8_t dBuf2[8192];
  uint32_t loaderIdx=0;

  DEBUG(0,"FPGA:Using onboard setup.");

  time = Timer_Get(0);

  // set PROG low to reset FPGA (open drain)
  IO_DriveLow_OD(PIN_FPGA_PROG_L); //AT91C_BASE_PIOA->PIO_OER = PIN_FPGA_PROG_L;

  SSC_EnableTxRx(); // start to drive config outputs
  Timer_Wait(1);
  IO_DriveHigh_OD(PIN_FPGA_PROG_L);  //AT91C_BASE_PIOA->PIO_ODR = PIN_FPGA_PROG_L;
  Timer_Wait(2);

  // check INIT is high
  if (IO_Input_L(PIN_FPGA_INIT_L)) {
    WARNING("FPGA:INIT is not high after PROG reset.");
    return 1;
  }
  // check DONE is low
  if (IO_Input_H(PIN_FPGA_DONE)) {
    WARNING("FPGA:DONE is high before configuration.");
    return 1;
  }

  // send FPGA data with SSC DMA in parallel to reading the file
  secCount = 0;
  do {
    uint8_t *pWBuf, *pRBuf;
    uint16_t cmp_status;
    uint32_t uncomp_len = sizeof(dBuf1); // does not matter which we use here, have to be the same!
    uint32_t cmp_len = (loader[loaderIdx]<<8) | (loader[loaderIdx+1]);
    loaderIdx += 2;

    // showing some progress...
    if (!((secCount++ >> 4) & 3)) {
      ACTLED_ON;
    } else {
      ACTLED_OFF;
    }

    if (secCount&1) {
      pWBuf=&(dBuf1[0]);
    } else {
      pWBuf=&(dBuf2[0]);
    }
    cmp_status = tinfl_decompress_mem_to_mem(pWBuf, uncomp_len, &(loader[loaderIdx]), cmp_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (cmp_status==-1) {
      WARNING("Bad FPGA configuration setup in FW");
      break;
    } else {
      uncomp_len=cmp_status;
    }
    loaderIdx+=cmp_len;
    DEBUG(3,"%d --> %d  (%08x,%08x,%d)",cmp_len,uncomp_len,pWBuf,&(loader[loaderIdx]),cmp_status);

    // take the just read buffer for writing
    pRBuf=pWBuf;
    SSC_WaitDMA();
    SSC_WriteBufferSingle(pRBuf, uncomp_len, 0);
  } while(loaderIdx < sizeof(loader));
  SSC_WaitDMA();

  // some extra clocks
  SSC_Write(0x00);
  //
  SSC_DisableTxRx();
  ACTLED_OFF;
  Timer_Wait(1);

  // check DONE is high
  if (!IO_Input_H(PIN_FPGA_DONE) ) {
    WARNING("FPGA:Failed to config fallback FPGA. Fatal, system will reboot");
    return 1;
  }
  else {
    time = Timer_Get(0)-time;

    DEBUG(0,"FPGA configured in %d ms.", (uint32_t) (time >> 20));
  }
  return 0;
}

uint8_t FPGA_Config(FF_FILE *pFile) // assume file is open and at start
{
  uint32_t bytesRead;
  uint32_t secCount;
  unsigned long time;

  DEBUG(2,"FPGA:Starting Configuration.");

  time = Timer_Get(0);

  // set PROG low to reset FPGA (open drain)
  IO_DriveLow_OD(PIN_FPGA_PROG_L); //AT91C_BASE_PIOA->PIO_OER = PIN_FPGA_PROG_L;

  SSC_Configure_Boot(); // TEMP MIKEJ SHOULD NOT BE NECESSARY

  SSC_EnableTxRx(); // start to drive config outputs
  Timer_Wait(1);
  IO_DriveHigh_OD(PIN_FPGA_PROG_L);  //AT91C_BASE_PIOA->PIO_ODR = PIN_FPGA_PROG_L;
  Timer_Wait(2);

  // check INIT is high
  if (IO_Input_L(PIN_FPGA_INIT_L)) {
    WARNING("FPGA:INIT is not high after PROG reset.");
    return 1;
  }
  // check DONE is low
  if (IO_Input_H(PIN_FPGA_DONE)) {
    WARNING("FPGA:DONE is high before configuration.");
    return 1;
  }

  // send FPGA data with SSC DMA in parallel to reading the file
  secCount = 0;
  do {
    uint8_t fBuf1[FILEBUF_SIZE];
    uint8_t fBuf2[FILEBUF_SIZE];
    uint8_t *pBufR;
    uint8_t *pBufW;

    // showing some progress...
    if (!((secCount++ >> 4) & 3)) {
      ACTLED_ON;
    } else {
      ACTLED_OFF;
    }

    // switch between 2 buffers to read-in
    if (secCount&1)
      pBufR = &(fBuf2[0]);
    else
      pBufR = &(fBuf1[0]);
    bytesRead = FF_Read(pFile, FILEBUF_SIZE, 1, pBufR);
    // take the just read buffer for writing
    pBufW = pBufR;
    SSC_WaitDMA();
    SSC_WriteBufferSingle(pBufW, bytesRead, 0);
  } while(bytesRead > 0);
  SSC_WaitDMA();

  // some extra clocks
  SSC_Write(0x00);
  //
  SSC_DisableTxRx();
  ACTLED_OFF;
  Timer_Wait(1);

  // check DONE is high
  if (!IO_Input_H(PIN_FPGA_DONE) ) {
    WARNING("FPGA:Failed to config FPGA.");
    return 1;
  }
  else {
    time = Timer_Get(0)-time;

    DEBUG(0,"FPGA configured in %d ms.", (uint32_t) (time >> 20));
  }
  return 0;
}

//
// Memory Test
//

uint8_t FPGA_DramTrain(void)
{
  // actually just dram test for now
  uint8_t mBuf[512];
  uint32_t i;
  uint32_t addr;
  DEBUG(1,"FPGA:DRAM enabled, running test.");
  // 25..0  64MByte
  // 25 23        15        7
  // 00 0000 0000 0000 0000 0000 0000

  memset(mBuf, 0, 512);
  for (i=0;i<128;i++) mBuf[i] = kMemtest[i];

  addr = 0;
  for (i=0;i<19;i++){
    mBuf[127] = (uint8_t) i;
    FileIO_MCh_BufToMem(mBuf, addr, 128);
    addr = (0x100 << i);
  }

  addr = 0;
  for (i=0;i<19;i++){
    memset(mBuf, 0xAA, 512);
    FileIO_MCh_MemToBuf(mBuf, addr, 128);
    if (memcmp(mBuf,&kMemtest[0],127) || (mBuf[127] != (uint8_t) i) ) {
      WARNING("!!Match fail Addr:%8X", addr);
      DumpBuffer(mBuf,128);
      return 1;
    }
    addr = (0x100 << i);
  }
  DEBUG(1,"FPGA:DRAM TEST passed.");
  return 0;
}

uint8_t FPGA_DramEye(uint8_t mode)
{
  uint32_t stat;
  uint32_t ram_phase;
  uint32_t ram_ctrl;
  uint16_t key;

  DEBUG(1,"FPGA:DRAM BIST stress.... this takes a while");

  ram_phase = kDRAM_PHASE;
  ram_ctrl  = kDRAM_SEL;
  OSD_ConfigSendCtrl((ram_ctrl << 8) | ram_phase );

  OSD_ConfigSendUserD(0x01000000); // enable BIST

  do {
    Timer_Wait(200);
    stat = OSD_ConfigReadStatus();

    if (mode !=0)
      DEBUG(1,"BIST stat: %02X", stat & 0x3C);

    if (mode == 0) {
      if (((stat & 0x38) >> 3) == 2) { // two passes

        if (stat & 0x04) {
          DEBUG(1,"FPGA:DRAM BIST **** FAILED !! ****");
        }
        else {
          DEBUG(1,"FPGA:DRAM BIST passed.");
        }
        break;
      }
    }

    key = OSD_GetKeyCode(1);

    if (key == KEY_LEFT) {
      ram_phase-=8;
      OSD_ConfigSendCtrl((ram_ctrl << 8) | ram_phase );
      OSD_ConfigSendUserD(0x00000000); // disable BIST
      OSD_ConfigSendUserD(0x01000000); // enable BIST

    }

    if (key == KEY_RIGHT) {
      ram_phase+=8;
      OSD_ConfigSendCtrl((ram_ctrl << 8) | ram_phase );
      OSD_ConfigSendUserD(0x00000000); // disable BIST
      OSD_ConfigSendUserD(0x01000000); // enable BIST
    }

  } while (key != KEY_MENU);
  OSD_ConfigSendUserD(0x00000000); // disable BIST

  return 0;
}


uint8_t FPGA_ProdTest(void)
{
  uint32_t ram_phase;
  uint32_t ram_ctrl;
  uint16_t key;

  DEBUG(0,"PRODTEST: phase 0");
  ram_phase = kDRAM_PHASE;
  ram_ctrl  = kDRAM_SEL;
  OSD_ConfigSendCtrl((ram_ctrl << 8) | ram_phase );
  FPGA_DramTrain();

  FPGA_DramEye(0); // /=0 for interactive

  // return to nominal
  ram_phase = kDRAM_PHASE;
  ram_ctrl  = kDRAM_SEL;
  OSD_ConfigSendCtrl((ram_ctrl << 8) | ram_phase );

  /*void OSD_ConfigSendUser(uint32_t configD, uint32_t configS)*/
  uint32_t testpat  = 0;
  uint32_t testnum  = 0;
  uint32_t maskpat  = 0;
  uint32_t vidpat   = 0;
  uint32_t vidstd   = 0;

  uint32_t stat     = 0;
  uint32_t config_d = 0;
  uint32_t config_s = 0;
  uint32_t failed   = 0;

  OSD_ConfigSendUserS(0); // default, no reset required

  do {
    config_d = ((maskpat & 0xF) << 18) |((vidpat & 0x3) << 16) | ((testpat & 0xFF) << 8) | (testnum & 0xFF);

    OSD_ConfigSendUserD(config_d);
    Timer_Wait(1);
    OSD_ConfigSendUserD(config_d | 0x80000000);
    Timer_Wait(1);
    OSD_ConfigSendUserD(config_d | 0x00000000);
    Timer_Wait(1);

    stat = OSD_ConfigReadStatus();
    if ((stat & 0x03) != 0x01) {
      WARNING("Test %02X Num %02X, ** FAIL **", testpat, testnum);
      failed ++;
    }

    testnum++;
    switch (testpat) {
      case 0x00 : if (testnum == 0x32) { testnum = 0; testpat ++;} break;
      case 0x01 : if (testnum == 0x06) { testnum = 0; testpat ++;} break;
      case 0x02 : if (testnum == 0x18) { testnum = 0; testpat ++;} break;
      case 0x03 : if (testnum == 0x18) { testnum = 0; testpat ++;} break;
      case 0x04 : if (testnum == 0x18) { testnum = 0; testpat ++;} break;
      case 0x05 : if (testnum == 0x16) { testnum = 0; testpat ++;} break;
      case 0x06 : if (testnum == 0x26) { testnum = 0; testpat ++;} break;
      case 0x07 : if (testnum == 0x0A) { testnum = 0; testpat ++;} break;
    }
    if (testpat == 8) {
      if (failed == 0) {
        DEBUG(0,"IO TEST PASS");
      }
      testpat = 0;
      failed = 0;
    }

    key = OSD_GetKeyCode(1);
    if (key == KEY_F8) {
      vidpat = (vidpat - 1) & 0x3;
    }
    if (key == KEY_F9) {
      vidpat = (vidpat + 1) & 0x3;
    }

    if (key == KEY_F6) {
      if    (maskpat == 8)
        maskpat = 0;
      else if (maskpat != 0)
        maskpat--;
      DEBUG(0,"Mask %01X", maskpat);
    }
    if (key == KEY_F7) {
      if    (maskpat == 0)
        maskpat = 8;
      else if (maskpat != 15)
        maskpat++;
      DEBUG(0,"Mask %01X", maskpat);
    }

    if ((key == KEY_F4) || (key == KEY_F5)) {

      if (key == KEY_F4) vidstd = (vidstd - 1) & 0x7;
      if (key == KEY_F5) vidstd = (vidstd + 1) & 0x7;

      // update, hard reset of FPGA

      IO_DriveLow_OD(PIN_FPGA_RST_L); // make sure FPGA is held in reset

      // set up coder/clock
      switch (vidstd) {
        case 0 : CFG_vid_timing_SD(F60HZ);   CFG_set_coder(CODER_NTSC);        break;
        case 1 : CFG_vid_timing_SD(F50HZ);   CFG_set_coder(CODER_PAL);         break;
        case 2 : CFG_vid_timing_SD(F60HZ);   CFG_set_coder(CODER_NTSC_NOTRAP); break;
        case 3 : CFG_vid_timing_SD(F50HZ);   CFG_set_coder(CODER_PAL_NOTRAP);  break;
        case 4 : CFG_vid_timing_HD27(F60HZ); CFG_set_coder(CODER_DISABLE);     break;
        case 5 : CFG_vid_timing_HD27(F50HZ); CFG_set_coder(CODER_DISABLE);     break;
        case 6 : CFG_vid_timing_HD74(F60HZ); CFG_set_coder(CODER_DISABLE);     break;
        case 7 : CFG_vid_timing_HD74(F50HZ); CFG_set_coder(CODER_DISABLE);     break;
      }
      DEBUG(0,"VidStd %01X", vidstd);
      Timer_Wait(200);

      IO_DriveHigh_OD(PIN_FPGA_RST_L); // release reset
      Timer_Wait(200);

      if ((vidstd == 6) || (vidstd == 7))
        CFG_set_CH7301_HD();
      else
        CFG_set_CH7301_SD();

      // resend config
      config_s = (vidstd & 0x7);
      OSD_ConfigSendUserS(config_s);
      OSD_ConfigSendUserD(config_d);
      Timer_Wait(10);

      // apply new static config
      OSD_Reset(OSDCMD_CTRL_RES);

      Timer_Wait(10);
    }

    Timer_Wait(5);

  } while (key != KEY_MENU);
  return 0;
}

//
// Replay Application Call (rApps):
//
// ----------------------------------------------------------------
//  we need this local/inline to avoid function calls in this stage
// ----------------------------------------------------------------

#define _SPI_EnableFileIO() { AT91C_BASE_PIOA->PIO_CODR=PIN_FPGA_CTRL0; }
#define _SPI_DisableFileIO() { while (!(AT91C_BASE_SPI->SPI_SR & AT91C_SPI_TXEMPTY)); AT91C_BASE_PIOA->PIO_SODR = PIN_FPGA_CTRL0; }

inline uint8_t _SPI(uint8_t outByte)
{
  volatile uint32_t t = AT91C_BASE_SPI->SPI_RDR;  // compiler warning, but is a must!
  while (!(AT91C_BASE_SPI->SPI_SR & AT91C_SPI_TDRE));
  AT91C_BASE_SPI->SPI_TDR = outByte;
  while (!(AT91C_BASE_SPI->SPI_SR & AT91C_SPI_RDRF));
  return((uint8_t)AT91C_BASE_SPI->SPI_RDR);
}

inline void _FPGA_WaitStat(uint8_t mask, uint8_t wanted)
{
  do {
    _SPI_EnableFileIO();
    _SPI(0x87); // do Read
    if ((_SPI(0) & mask) == wanted) break;
    _SPI_DisableFileIO();
  } while (1);
  _SPI_DisableFileIO();
}

inline void _SPI_ReadBufferSingle(void *pBuffer, uint32_t length)
{
  AT91C_BASE_SPI->SPI_TPR  = (uint32_t) pBuffer;
  AT91C_BASE_SPI->SPI_TCR  = length;
  AT91C_BASE_SPI->SPI_RPR  = (uint32_t) pBuffer;
  AT91C_BASE_SPI->SPI_RCR  = length;
  AT91C_BASE_SPI->SPI_PTCR = AT91C_PDC_TXTEN | AT91C_PDC_RXTEN;
  while ((AT91C_BASE_SPI->SPI_SR & (AT91C_SPI_ENDTX | AT91C_SPI_ENDRX)) != (AT91C_SPI_ENDTX | AT91C_SPI_ENDRX) ) {};
}

void FPGA_ExecMem(uint32_t base, uint16_t len, uint32_t checksum)
{
  uint32_t i, j, l=0, sum=0;
  volatile uint32_t *dest = (volatile uint32_t *)0x00200000L;
  uint8_t value;

  DEBUG(0,"FPGA:copy %d bytes from 0x%lx and execute if the checksum is 0x%lx",len,base,checksum);
  DEBUG(0,"FPGA:we have about %ld bytes free for the code",((uint32_t)&value)-0x00200000L);

  if ((((uint32_t)&value)-0x00200000L)<len) {
    WARNING("FPGA: Not enough memory, processor may crash!");
  }

  _SPI_EnableFileIO();
  _SPI(0x80); // set address
  _SPI((uint8_t)(base));
  _SPI((uint8_t)(base >> 8));
  _SPI((uint8_t)(base >> 16));
  _SPI((uint8_t)(base >> 24));
  _SPI_DisableFileIO();

  _SPI_EnableFileIO();
  _SPI(0x81); // set direction
  _SPI(0x80); // read
  _SPI_DisableFileIO();

  // LOOP FOR BLOCKS TO READ TO SRAM
  for(i=0;i<(len/512)+1;++i) {
    uint32_t buf[128];
    uint32_t *ptr = &(buf[0]);
    _SPI_EnableFileIO();
    _SPI(0x84); // read first buffer, FPGA stalls if we don't read this size
    _SPI((uint8_t)( 512 - 1));
    _SPI((uint8_t)((512 - 1) >> 8));
    _SPI_DisableFileIO();
    _FPGA_WaitStat(0x04, 0);
    _SPI_EnableFileIO();
    _SPI(0xA0); // should check status
    _SPI_ReadBufferSingle(buf, 512);
    _SPI_DisableFileIO();
    for(j=0;j<128;++j) {
      // avoid summing up undefined data in the last block
      if (l<len) sum += *ptr++;
      else break;
      l+=4;
    }
  }

  // STOP HERE
  if (sum!=checksum) {
    ERROR("FPGA:CHK exp: 0x%lx got: 0x%lx",checksum,sum);
    dest = (volatile uint32_t *)0x00200000L;
    DEBUG(0,"FPGA:<-- 0x%08lx",*(dest));
    DEBUG(0,"FPGA:<-- 0x%08lx",*(dest+1));
    DEBUG(0,"FPGA:<-- 0x%08lx",*(dest+2));
    DEBUG(0,"FPGA:<-- 0x%08lx",*(dest+3));
    return;
  }

  // NOW COPY IT TO RAM!
  // no variables in mem from here...

  sum=0;
  dest = (volatile uint32_t *)0x00200000L;
  DEBUG(0,"FPGA:SRAM start: 0x%lx (%d blocks)",(uint32_t)dest,1+len/512);
  Timer_Wait(500); // take care we can send this message before we go on!

  _SPI_EnableFileIO();
  _SPI(0x80); // set address
  _SPI((uint8_t)(base));
  _SPI((uint8_t)(base >> 8));
  _SPI((uint8_t)(base >> 16));
  _SPI((uint8_t)(base >> 24));
  _SPI_DisableFileIO();

  _SPI_EnableFileIO();
  _SPI(0x81); // set direction
  _SPI(0x80); // read
  _SPI_DisableFileIO();

  // LOOP FOR BLOCKS TO READ TO SRAM
  for(i=0;i<(len/512)+1;++i) {
    _SPI_EnableFileIO();
    _SPI(0x84); // read first buffer, FPGA stalls if we don't read this size
    _SPI((uint8_t)( 512 - 1));
    _SPI((uint8_t)((512 - 1) >> 8));
    _SPI_DisableFileIO();
    _FPGA_WaitStat(0x04, 0);
    _SPI_EnableFileIO();
    _SPI(0xA0); // should check status
    _SPI_ReadBufferSingle((void *)dest, 512);
    _SPI_DisableFileIO();
    for(j=0;j<128;++j) *dest++;
  }
  // execute from SRAM the code we just pushed in
  asm("ldr r3, = 0x00200000\n");
  asm("bx  r3\n");
}

