//
// File navigator and TIFF/JPEG image viewer demo sketch
// written by Larry Bank
//
#include <SPI.h>
#include <AnimatedGIF.h>
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <OneBitDisplay.h>
#include <TIFF_G4.h>
#include "Roboto_Black_70.h"
#include <M5FastEPD.h>
#include <SD.h>
#include <stdlib.h>
#include "DejaVu_Sans_Mono_Bold_16.h"
#include "DejaVu_Sans_Mono_Bold_28.h"

const GFXfont *pSmallFont = &DejaVu_Sans_Mono_Bold_28;
const GFXfont *pBigFont = &Roboto_Black_70;
bool bRotated = true;
JPEGDEC *jpg;
PNG *png;
AnimatedGIF *gif;
TIFFG4 *tif;
M5FastEPD EPD = M5FastEPD();
ONE_BIT_DISPLAY obd;
#define DISPLAY_WIDTH 960
#define DISPLAY_HEIGHT 540
#define NAMES_TOP_Y 64
#define NAMES_LEFT_X 8
#define TEXT_HEIGHT 32
static uint8_t ucBackBuffer[((DISPLAY_WIDTH+7)*(DISPLAY_HEIGHT+7))/8]; // 1-bpp 'canvas' for OneBitDisplay
static uint8_t ucTemp[DISPLAY_WIDTH*2]; // one line
static int iDisplayWidth, iDisplayHeight;
void ConvertOBD(int sx, int sy, int width, int height, int ibpp);
static int iOldSelected;
int iXOff, iYOff;
static int iIdleCount;
// one minute of inactivity (10 ms ticks)
#define IDLE_TIMEOUT (100 * 60)
#define RKEY_EXIT  1
#define RKEY_LEFT  2
#define RKEY_RIGHT 4
#define RKEY_UP    8
#define RKEY_DOWN  16
#define RKEY_BUTT1 32

#define M5PAPER_UP 37
#define M5PAPER_DOWN 39
#define M5PAPER_ENTER 38
#define SD_CS 4
#define SD_SCK 14
#define SD_MOSI 12
#define SD_MISO 13
#define M5EPD_CS_PIN 15
#define M5EPD_SCK_PIN 14
#define M5EPD_MOSI_PIN 12
#define M5EPD_BUSY_PIN 27
#define M5EPD_MISO_PIN 13

volatile uint32_t u32PrevBits, u32GPIOBits;

void SG_Rectangle(int x, int y, int w, int h, uint16_t usColor, int bFill)
{
    if (bFill)
       obd.fillRect(x, y, w, h, usColor);
    else
       obd.drawRect(x, y, w, h, usColor);
} /* SG_Rectangle() */

void SG_WriteString(int x, int y, char *pString, uint16_t usFGColor, uint16_t usBGColor, int bLarge)
{
  int16_t rx, ry;
  uint16_t rw, rh;
   obd.setTextColor(OBD_BLACK, OBD_WHITE); //usFGColor, usBGColor);
   obd.setFreeFont((bLarge) ? pBigFont : pSmallFont);
   //obd.setFont((bLarge) ? FONT_12x16 : FONT_8x8);
   if (!bLarge) y -= 8;
   obd.setCursor(x, y + NAMES_TOP_Y);
   obd.print(pString);
   obd.getTextBounds(pString, x, y, &rx, &ry, &rw, &rh); 
   //SG_Rectangle(rx, ry, rw, rh, usFGColor, false); // selection box
//   Serial.println(pString);
} /* SG_WriteString() */

void GUIDrawNames(char *pDir, char *pDirNames, char *pFileNames, int *pSizes, int iDirCount, int iFileCount, int iSelected)
{
int i, iLen, iNumLines, iNumCols;
int iCurrent;
unsigned short usFG, usBG;
char szTemp[512];

        SG_WriteString(6,0,(char*)"SD File Explorer", OBD_BLACK, OBD_WHITE, 1);
        iNumLines = (iDisplayHeight - (NAMES_TOP_Y + TEXT_HEIGHT))/TEXT_HEIGHT;
        iNumCols = 30; //(iDisplayWidth*3)/(TEXT_HEIGHT*2);
        strcpy(szTemp, pDir);
        strcat(szTemp, "                                        ");
        szTemp[iNumCols-2] = 0;
        SG_WriteString(8,TEXT_HEIGHT,szTemp,0xffff,0x1f,0);
        if (iSelected >= iNumLines) iCurrent = iSelected-(iNumLines-1);
        else iCurrent = 0;
        for (i=0; i<(iDirCount + iFileCount); i++) // draw all lines on the display
        {
           usBG = 0; // black background
           if (iCurrent >= (iDirCount+iFileCount)) // short list, fill with spaces
           {
                 strcpy(szTemp, (char*)"                                         ");
           }
           else
           {
              if (iCurrent >= iDirCount) // use filenames
              {
                 strcpy(szTemp, &pFileNames[(iCurrent-iDirCount)*256]);
              }
              else // use dir names
              {
                 strcpy(szTemp, &pDirNames[iCurrent*256]);
              }
              iLen = strlen(szTemp);
//              if (iLen < iNumCols) // fill with spaces to erase old data
//              {
//                 strcat(szTemp, "                                        ");
//              }
//              szTemp[iNumCols-2] = 0;
             usFG = (iCurrent == iSelected) ? OBD_WHITE : OBD_BLACK;
             usBG = (iCurrent == iSelected) ? OBD_BLACK : OBD_WHITE;
           }
// remove any ~ characters because they're missing from the custom fonts
//              for (int j=0; j<iLen; j++) {
//                if (szTemp[j] > '}' || szTemp[j] < 32) szTemp[j] = '_'; // substitute an underscore
//              }
              if (iCurrent >= iDirCount) // use filenames
              {
                 int iKBytes = (pSizes[iCurrent-iDirCount] >> 10);
                 char szTemp2[16]; 
                 strcat(szTemp, "                                                   "); // to erase any old text
                 if (iKBytes == 0) iKBytes = 1;
                 usFG = 0x7e0; //0x7ff; // cyan text
                 strcpy(szTemp, &pFileNames[(iCurrent-iDirCount)*256]);
                 strcat(szTemp, "                                                  ");          
                 if (iKBytes > 1024) {
                   iKBytes >>= 10;
                   if (iKBytes < 1) iKBytes = 1;
                   sprintf(szTemp2, "%dM", iKBytes);
                 } else {
                   sprintf(szTemp2, "%dK", iKBytes);
                 }
                 strcpy(&szTemp[iNumCols-1-strlen(szTemp2)], szTemp2);
              } else {
                 strcat(szTemp, "        "); // to erase any old text
              }
           SG_WriteString(NAMES_LEFT_X, NAMES_TOP_Y+(i*TEXT_HEIGHT), szTemp, usFG, usBG, 0);
//           Serial.println(szTemp);
           iCurrent++;
        }
} /* GUIDrawNames() */

void UpdateGPIOButtons(void)
{
  u32GPIOBits = 0;

  if (digitalRead(M5PAPER_UP) == 0)
     u32GPIOBits |= RKEY_UP;
  if (digitalRead(M5PAPER_DOWN) == 0)
     u32GPIOBits |= RKEY_DOWN;
  if (digitalRead(M5PAPER_ENTER) == 0)
     u32GPIOBits |= RKEY_BUTT1;
} /* UpdateGPIOButtons() */

void GetLeafName(char *fname, char *leaf)
{
int i, iLen;

   iLen = strlen(fname);
   for (i=iLen-1; i>=0; i--)
      {
      if (fname[i] == '/')
         break;
      }
   strcpy(leaf, &fname[i+1]);
} /* GetLeafName() */

//
// Adjust the give path to point to the parent directory
//
void GetParentDir(char *szDir)
{
int i, iLen;
        iLen = strlen(szDir);
        for (i=iLen-1; i>=0; i--)
        {
                if (szDir[i] == '/') { // look for the next slash 'up'
                   szDir[i] = 0;
                   break;
                }
        }
        if (strlen(szDir) == 0)
           strcat(szDir, "/"); // we hit the root dir
} /* GetParentDir() */

int name_compare(const void *ina, const void *inb)
{
char *a = (char *)ina;
char *b = (char *)inb;

 while (*a && *b) {
        if (tolower(*a) != tolower(*b)) {
            break;
        }
        ++a;
        ++b;
    }
    return (int)(tolower(*a) - tolower(*b));
} /* name_compare() */

// Increment the idle counter
void IncrementIdle(void)
{
  iIdleCount++;
  if (iIdleCount > IDLE_TIMEOUT) {
     EPD.Power(false); // turn off EPD
     deepSleep(1000 * 60 * 60 * 24 * 7); // sleep for a week
  }
}

void ClearIdle(void)
{
  iIdleCount = 0;
}
void UpdateDisplay(int x, int y, int w, int h, bool bFullUpdate)
{
      ConvertOBD(x,y,w, h, 4);
      if (bFullUpdate) {
        EPD.UpdateFull(UPDATE_MODE_GC16);
      } else {
        EPD.UpdateArea(y, iDisplayWidth-x-w, h, w, UPDATE_MODE_GC16); //UPDATE_MODE_DU4); //UPDATE_MODE_GLD16);
      }
} /* UpdateDisplay() */

//
// Calculate the update rectangle to just draw the name being deselected and newly selected
//
void UpdateSelection(int iSelected)
{
int x, y, w, h;

  y = NAMES_TOP_Y+(iSelected+1)*TEXT_HEIGHT;
  h = 2 * TEXT_HEIGHT;
  ConvertOBD(0, y, (iDisplayWidth & 0xfff8), h, 4);
  EPD.UpdateArea(y, 0, h, iDisplayWidth & 0xfff8, UPDATE_MODE_A2);
//  EPD.UpdateArea(x, y, w, h, UPDATE_MODE_DU);
}
void NavigateFiles(char *cDir, char *szDestName)
{
File root, dir;
int iSelected;
int iReturn = 0;
int iDirCount, iFileCount, iDir, iFile;
int bDone = 0;
int iRepeat = 0;
char *pDirNames = NULL;
char *pFileNames = NULL;
int *pSizes = NULL;
char szTemp[256];
uint32_t u32Quit;
int iMaxSelection;
int bDirValid;


   obd.fillScreen(OBD_WHITE);
   iDir = iFile = iDirCount = iMaxSelection = iFileCount = 0;
      while (!bDone)
   {
      root = SD.open(cDir);
      if (root)
      {
         dir = root.openNextFile();
         if (dir)
         {
            // count the number of non-hidden directories and files
            iDirCount = 1;
            iFileCount = 0;
            while (dir)
            { 
              GetLeafName((char *)dir.name(), szTemp);
              if (dir.isDirectory() && szTemp[0] != '.')
                iDirCount++;
              else if (!dir.isDirectory() && szTemp[0] != '.')
                iFileCount++;
              dir.close();
              dir = root.openNextFile();
              delay(5);
            }
            root.rewindDirectory();
            if (pDirNames)
            {  
               free(pDirNames);
               free(pFileNames);
               free(pSizes);
            }
            pDirNames = (char *)malloc(256 * (iDirCount+1));
            pFileNames = (char *)malloc(256 * (iFileCount+1));
            pSizes = (int *)malloc((iFileCount+1) * sizeof(int));
            // now capture the names
            iDir = 1; // store ".." as the first dir
            strcpy(pDirNames, "..");
            iFile = 0;
            dir = root.openNextFile();
            while (dir)
            {  
               GetLeafName((char *)dir.name(), szTemp);
               if (dir.isDirectory() && szTemp[0] != '.')
               {  
                  strcpy(&pDirNames[iDir*256], szTemp);
                  iDir++;
               } 
               else if (!dir.isDirectory() && szTemp[0] != '.')
               {
                 pSizes[iFile] = dir.size();
                 strcpy(&pFileNames[iFile*256], szTemp);
                 iFile++;
               }
               dir.close();
               dir = root.openNextFile();
               delay(5);
            }
         }
         root.close();
         iDirCount = iDir;
         iFileCount = iFile; // get the usable names count
         iMaxSelection = iDirCount + iFileCount;
         Serial.printf("dirs = %d, files = %d\n", iDirCount, iFileCount);
         Serial.flush();
        // Sort the names
       // qsort(pDirNames, iDirCount, 256, name_compare);
       // qsort(pFileNames, iFileCount, 256, name_compare);
      } // dir opened
restart:
      iSelected = 0;
      GUIDrawNames(cDir, pDirNames, pFileNames, pSizes, iDirCount, iFileCount, iSelected);
      InvertRect(0, NAMES_TOP_Y+TEXT_HEIGHT, iDisplayWidth, TEXT_HEIGHT);
      UpdateDisplay(0,0,iDisplayWidth, iDisplayHeight, false); // redraw everything

      bDirValid = 1;
      while (bDirValid)
      {
         UpdateGPIOButtons();
         if (u32GPIOBits == u32PrevBits)
         {
            IncrementIdle();
            lightSleep(10); // save some power while we wait
            iRepeat++;
            if (iRepeat < 100) // 1 second starts a repeat
               continue;
         }
         else // change means cancel repeat
         {
            iRepeat = 0;
            ClearIdle();
         }
         if (((u32GPIOBits & u32Quit) == u32Quit) && ((u32PrevBits & u32Quit) != u32Quit))
         { // quit SmartGear signal
//                int rc, i = QuitMenu();
//                if (i==0){bDirValid = 0; continue;}
//              else if (i==1) {Terminal(); continue;}
//                else if (i==2) {bDirValid=0; bDone=1; continue;}
//              else if (i==3) {spilcdFill(0); rc = system("sudo shutdown now");}
//              else if (i==4) rc = system("sudo reboot");
//                else {bDirValid=0; continue;} // continue after 'About'
//                if (rc < 0) {};
         }
        if (((u32GPIOBits & RKEY_EXIT) == RKEY_EXIT) && ((u32PrevBits & RKEY_EXIT) != RKEY_EXIT))
        {
        // quit menu - 0=resume, 1=quit, 2=shutdown
        }
        if (u32GPIOBits & RKEY_UP && (iRepeat || !(u32PrevBits & RKEY_UP)))
        { // navigate up
            if (iSelected > 0)
            {
               InvertRect(0, NAMES_TOP_Y+(iSelected+1)*TEXT_HEIGHT, iDisplayWidth, TEXT_HEIGHT);
               iSelected--;
               InvertRect(0, NAMES_TOP_Y+(iSelected+1)*TEXT_HEIGHT, iDisplayWidth, TEXT_HEIGHT);
//               GUIDrawNames(cDir, pDirNames, pFileNames, iDirCount, iFileCount, iSelected);
               UpdateSelection(iSelected);
            }
         }
         if (u32GPIOBits & RKEY_DOWN && (iRepeat || !(u32PrevBits & RKEY_DOWN)))
         { // navigate down
            if (iSelected < iMaxSelection-1)
            {
               InvertRect(0, NAMES_TOP_Y+(iSelected+1)*TEXT_HEIGHT, iDisplayWidth, TEXT_HEIGHT);
               iSelected++;
               InvertRect(0, NAMES_TOP_Y+(iSelected+1)*TEXT_HEIGHT, iDisplayWidth, TEXT_HEIGHT);
//               GUIDrawNames(cDir, pDirNames, pFileNames, iDirCount, iFileCount, iSelected);
               UpdateSelection(iSelected-1);
            }
         }
         if (u32GPIOBits & RKEY_BUTT1 && !(u32PrevBits & RKEY_BUTT1))
         {
            bDirValid = 0;
            if (iSelected == 0) // the '..' dir goes up 1 level
            {
               if (strcmp(cDir, "/") != 0) // navigating beyond root will mess things up
               {
                  GetParentDir(cDir);
               }
            }
            else
            {
               if (iSelected < iDirCount) // user selected a directory
               {
                  if (strcmp(cDir, "/") != 0)
                     strcat(cDir, "/");
                  strcat(cDir, &pDirNames[iSelected*256]);
               }
               else // user selected a file, leave
               {
//                  strcpy(szDestName, "/sd");
                  strcpy(szDestName, "/");
                  if (strcmp(cDir, "/") != 0)
                     strcat(szDestName, "/");
                  strcat(szDestName, &pFileNames[(iSelected-iDirCount)*256]);
                  bDone = 1; // exit the main loop
                  iReturn = 1;
               }
            }
         }
         u32PrevBits = u32GPIOBits;
      } // while bDirValid
   }
   SG_Rectangle(0,0,iDisplayWidth, iDisplayHeight, 0, 1); // erase to black before starting
} /* NavigateFiles() */

// Functions to access a file on the SD card
File myfile;

void * myOpen(const char *filename, int32_t *size) {
  myfile = SD.open(filename);
  *size = myfile.size();
  return &myfile;
}
void myClose(void *handle) {
  if (myfile) myfile.close();
}

int32_t TIFFRead(TIFFFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}
int32_t TIFFSeek(TIFFFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}

int32_t myRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}
int32_t mySeek(JPEGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}
int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{ 
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
       return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
} /* GIFReadFile() */

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
} /* GIFSeekFile() */

int32_t pngRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}
int32_t pngSeek(PNGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}

// Function to draw pixels to the display
void PNGDraw(PNGDRAW *pDraw) {
  uint16_t u16, *s;
  uint8_t *d;
  int x, g0, g1;
  // easier to use this than to rewrite all of that code
  png->getLineAsRGB565(pDraw, (uint16_t *)ucTemp, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  s = (uint16_t *)ucTemp;
  d = ucTemp;
  for (x=0; x<pDraw->iWidth; x+=2) {
      u16 = *s++;
      g0 = (u16 & 0x7e0) >> 5; // calculate gray level
      g0 += (u16 & 0x1f);
      g0 += ((u16 & 0xf800) >> 11);
      g0 = (g0 >> 3) ^ 0xf; // get 4-bit value and invert
      u16 = *s++;
      g1 = (u16 & 0x7e0) >> 5; // calculate gray level
      g1 += (u16 & 0x1f);
      g1 += ((u16 & 0xf800) >> 11);
      g1 = (g1 >> 3) ^ 0xf; // get 4-bit value and invert
      *d++ = (uint8_t)((g0 << 4) | g1);
  }
  EPD.WritePartGram4bpp(iXOff, iYOff + pDraw->y, (pDraw->iWidth + 7) & 0xfff8, 1, (const uint8_t *)ucTemp); // This writes one line into the EPD framebuffer
} /* PNGDraw() */

void GIFDraw(GIFDRAW *pDraw)
{
    uint8_t *s, *d;
    uint16_t u16, *usPalette;
    int x, g0, g1, iWidth; 
    
    iWidth = pDraw->iWidth;
    if (iWidth > DISPLAY_WIDTH) 
       iWidth = DISPLAY_WIDTH;
    usPalette = pDraw->pPalette;
    // Since we only render the first frame, no need to worry about transparency
    s = pDraw->pPixels;
    d = ucTemp;
    for (x=0; x<iWidth; x+=2) {
       u16 = usPalette[*s++];
       g0 = (u16 & 0x7e0) >> 5; // 6 bits of green
       g0 += (u16 >> 11); // 5 bits of red
       g0 += (u16 & 0x1f); // 5 bits of blue
       g0 = ~(g0 >> 3); // keep 4 bits and invert
       u16 = usPalette[*s++];
       g1 = (u16 & 0x7e0) >> 5; // 6 bits of green
       g1 += (u16 >> 11); // 5 bits of red
       g1 += (u16 & 0x1f); // 5 bits of blue
       g1 = ~(g1 >> 3); // keep 4 bits and invert
       *d++ = (g0 << 4) | g0;
    }
    EPD.WritePartGram4bpp(iXOff, iYOff + pDraw->y, (pDraw->iWidth + 7) & 0xfff8, 1, (const uint8_t *)ucTemp); // This writes one line into the EPD framebuffer
} /* GIFDraw() */

void TIFFDraw(TIFFDRAW *pDraw)
{ 
  int x;
  uint8_t *d, *s, uc, out;
  // need to invert the colors
  s = pDraw->pPixels;
  d = ucTemp;
  for (x=0; x<pDraw->iWidth+7; x+=8) {
    //ucTemp[x] = ~ucTemp[x];
    uc = *s++;
    out = 0xff;
    if (uc & 0x80) out ^= 0xf0;
    if (uc & 0x40) out ^= 0xf;
    *d++ = out;
    out = 0xff;
    if (uc & 0x20) out ^= 0xf0;
    if (uc & 0x10) out ^= 0xf;
    *d++ = out;
    out = 0xff;
    if (uc & 0x8) out ^= 0xf0;
    if (uc & 0x4) out ^= 0xf;
    *d++ = out;
    out = 0xff;
    if (uc & 0x2) out ^= 0xf0;
    if (uc & 0x1) out ^= 0xf;
    *d++ = out;
  }
  EPD.WritePartGram4bpp(pDraw->iDestX, pDraw->iDestY + pDraw->y,  (pDraw->iWidth + 7) & 0xfff8, 1, (const uint8_t *)ucTemp); // This writes one line into the EPD framebuffer
} /* TIFFDraw() */

// Function to draw pixels to the display
int JPEGDraw(JPEGDRAW *pDraw) {
  int x, y;
  uint8_t *s, uc;

  // Prepare rows of 4-bpp pixels to write to the EPD controller
  if (bRotated) {
     s = (uint8_t *)pDraw->pPixels;
     for (y=pDraw->y; y < pDraw->y + pDraw->iHeight; y++) {
        for (x=0; x<pDraw->iWidth/2; x++) {
          s[x] = ~s[x]; // colors are inverted relative to e-paper
        }
        EPD.WritePartGram4bpp(pDraw->x, y, (pDraw->iWidth + 7) & 0xfff8, 1, (const uint8_t *)s); // This writes one line into the EPD framebuffer
        s += pDraw->iWidth/2;
     }
  } else {

  }
  return 1;
} /* JPEGDraw() */
//
// Invert a rectangle
//
void InvertRect(int x, int y, int w, int h)
{
  uint8_t *s, u8Mask;
  int xx, yy;
  
  for (xx=0; xx<w; xx++) {
    s = &ucBackBuffer[((y >> 3) * iDisplayWidth) + x + xx];
    u8Mask = 1 << (y & 7);
    for (yy = y; yy < y+h; yy++) {
      s[0] ^= u8Mask;
      u8Mask <<= 1;
      if (u8Mask == 0) {
        u8Mask = 1; // next byte row
        s += iDisplayWidth;
      }
    } // for yy
  } // for xx
}
//
// Convert 1-bpp canvas to 2-bpp for the EPD
//
void ConvertOBD(int sx, int sy, int width, int height, int iBpp)
{
uint8_t *s, *d, ucSrcMask, uc, uc1;
int rc = M5EPD_OK;

if (sx < 0 || sy < 0 || sx >= iDisplayWidth || sy >= iDisplayHeight) return;
if (sx + width > iDisplayWidth) width = iDisplayWidth - sx;
if (sy + height > iDisplayHeight) height = iDisplayHeight - sy;

    if (bRotated) {
      if (iBpp == 4) {
      for (int x=sx+width-1; x>=sx && rc == M5EPD_OK; x--) {
        uint8_t ucDestMask = 0xf0;
        s = &ucBackBuffer[((sy >> 3) * iDisplayWidth) + x];
        d = ucTemp;
        uc = 0;
        ucSrcMask = 1<<(sy & 7);
        for (int y=sy; y<sy+height; y++) { //2 pixels per byte
          if (s[0] & ucSrcMask) uc |= ucDestMask;
          ucDestMask >>= 4;
          if (ucDestMask == 0) { // emit byte
            *d++ = uc;
            uc = 0;
            ucDestMask = 0xf0;
          }
          ucSrcMask <<= 1;
          if (ucSrcMask == 0) {
            ucSrcMask = 1;
            s += iDisplayWidth;
          }
        } // for y
        rc = EPD.WritePartGram4bpp(sy, (sx+width-1-x), height, 1, ucTemp); // This writes one line into the EPD framebuffer
      } // for x
      if (rc != M5EPD_OK) {
        Serial.printf("WritePartGram4bpp returned %d, x=%d, y=%d, w=%d, h=%d\n", rc, sx, sy, width, height);
      }
      } else { // 2bpp
      for (int x=sx+width-1; x>=sx && rc == M5EPD_OK; x--) {
        uint8_t ucDestMask = 0xc0;
        s = &ucBackBuffer[((sy >> 3) * iDisplayWidth) + x];
        d = ucTemp;
        uc = 0;
        ucSrcMask = 1<<(sy & 7);
        for (int y=sy; y<sy+height; y++) { // 4 pixels per byte
          if (s[0] & ucSrcMask) uc |= ucDestMask;
          ucDestMask >>= 2;
          if (ucDestMask == 0) { // emit byte
            *d++ = uc;
            uc = 0;
            ucDestMask = 0xc0;
          }
          ucSrcMask <<= 1;
          if (ucSrcMask == 0) {
            ucSrcMask = 1;
            s += iDisplayWidth;
          }
        } // for y
        rc = EPD.WritePartGram2bpp(sy, (sx+width-1-x), height, 1, ucTemp); // This writes one line into the EPD framebuffer
      } // for x
      if (rc != M5EPD_OK) {
        Serial.printf("WritePartGram2bpp returned %d, x=%d, y=%d, w=%d, h=%d\n", rc, sx, sy, width, height);
      }
      } // 2bpp
    } else {
      if (iBpp == 4) {
        for (int y=sy; y<sy+height; y++) {
          s = &ucBackBuffer[(y >> 2) * DISPLAY_WIDTH];
          ucSrcMask = 1<<(y & 7);
          d = ucTemp;
          for (int x=sx; x<sx+width; x+=2) { // 2 pixels per byte
            uc = 0;
            if (s[x] & ucSrcMask) uc |= 0xf0; 
            if (s[x+1] & ucSrcMask) uc |= 0x0f;
            *d++ = uc;
          } // for x
          EPD.WritePartGram4bpp(sx, y, width, 1, ucTemp); // this writes into the controller RAM
        } // for y
      } else { // 2bpp
        for (int y=sy; y<sy+height; y++) {
          s = &ucBackBuffer[(y >> 3) * DISPLAY_WIDTH];
         ucSrcMask = 1<<(y & 7);
         d = ucTemp;
         for (int x=sx; x<sx+width; x+=4) { // 4 pixels per byte
           uc = 0;
            if (s[x] & ucSrcMask) uc |= 0xc0; 
            if (s[x+1] & ucSrcMask) uc |= 0x30;
            if (s[x+2] & ucSrcMask) uc |= 0xc; 
            if (s[x+3] & ucSrcMask) uc |= 0x3;
            *d++ = uc;
          } // for x
          EPD.WritePartGram2bpp(sx, y, width, 1, ucTemp); // This writes one line into the EPD framebuffer
        } // for y
      } // 2bpp
  } // if not rotated
} /* ConvertOBD() */

int ViewFile(char *szName)
{
int rc, x, y, w, h, iScale, iOptions;
uint8_t *pDitherBuffer;

    // Try to open the file
    Serial.printf("Opening %s\n", szName);
    rc = strlen(szName);
    if (strcmp(&szName[rc-3], "jpg") == 0) {
    jpg = (JPEGDEC *)malloc(sizeof(JPEGDEC));
    if (!jpg) return 0;
    rc = jpg->open((const char *)szName, myOpen, myClose, myRead, mySeek, JPEGDraw);
    if (rc) {
      Serial.printf("jpeg opened, size = %dx%d\n", jpg->getWidth(), jpg->getHeight());
      EPD.Clear(true);
      // See if we need to scale down the image to fit the display
      iScale = 1; iOptions = 0;
      w = jpg->getWidth(); h = jpg->getHeight();
      if (w >= DISPLAY_WIDTH*8 || h >= DISPLAY_HEIGHT * 8) {
         iScale = 8;
         iOptions = JPEG_SCALE_EIGHTH;
      } else if (w >= DISPLAY_WIDTH * 4 || h >= DISPLAY_HEIGHT * 4) {
         iScale = 4;
         iOptions = JPEG_SCALE_QUARTER;
      } else if (w >= DISPLAY_WIDTH * 2 || h >= DISPLAY_HEIGHT * 2) {
         iScale = 2;
         iOptions = JPEG_SCALE_HALF;
      }
      // Center the image on the display
      x = (DISPLAY_WIDTH - w/iScale) / 2;
      if (x < 0) x = 0;
      y = (DISPLAY_HEIGHT - h/iScale) / 2;
      if (y < 0) y = 0;
      Serial.printf("image offset: %d,%d\n", x, y);
      jpg->setPixelType(FOUR_BIT_DITHERED);
      pDitherBuffer = (uint8_t *)malloc((w+16) * 16);
      jpg->decodeDither(x, y, pDitherBuffer, iOptions);
      jpg->close();
      free(pDitherBuffer);
      free(jpg);
      EPD.UpdateFull(UPDATE_MODE_GC16);
      Serial.println("Finished jpeg decode");
      return 1;
    } // jpeg opened
    } else if (strcmp(&szName[rc-3], "png") == 0) {
      png = (PNG *)malloc(sizeof(PNG));
      if (!png) return 0;
        rc = png->open((const char *)szName, myOpen, myClose, pngRead, pngSeek, PNGDraw); 
       if (rc == PNG_SUCCESS) {
          EPD.Clear(true);
          w = png->getWidth();
          h = png->getHeight();
          Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", w, h, png->getBpp(), png->getPixelType());
          // Center the image on the display
          x = (DISPLAY_WIDTH - w) / 2;
          if (x < 0) x = 0;
          y = (DISPLAY_HEIGHT - h) / 2;
          if (y < 0) y = 0;
          iXOff = x; iYOff = y; // DEBUG
          rc = png->decode(NULL, 0);
          png->close();
          free(png);
          EPD.UpdateFull(UPDATE_MODE_GC16);
          Serial.println("Finished png decode");
       }
    } else if (strcmp(&szName[rc-3], "gif") == 0) {
      gif = (AnimatedGIF *)malloc(sizeof(AnimatedGIF));
      if (!gif) return 0;
      gif->begin(LITTLE_ENDIAN_PIXELS);
      if (gif->open(szName, myOpen, myClose, GIFReadFile, GIFSeekFile, GIFDraw)) {
         iXOff = (DISPLAY_WIDTH - gif->getCanvasWidth())/2;
         if (iXOff < 0) iXOff = 0;
         iYOff = (DISPLAY_HEIGHT - gif->getCanvasHeight())/2;
         if (iYOff < 0) iYOff = 0;
         Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif->getCanvasWidth(), gif->getCanvasHeight());
         gif->playFrame(false, NULL); // decode one frame
         gif->close();
         free(gif);
         EPD.UpdateFull(UPDATE_MODE_GC16);
         Serial.println("Finished gif decode");
      } // gif opened
    } else if (strcmp(&szName[rc-3], "tif") == 0) {
      tif = (TIFFG4 *)malloc(sizeof(TIFFG4));
      if (!tif) return 0;
      rc = tif->openTIFF(szName, myOpen, myClose, TIFFRead, TIFFSeek, TIFFDraw);
      if (rc) {
         w = tif->getWidth();
         h = tif->getHeight();
          Serial.printf("TIFF opened, size = (%d x %d)\n", w, h);
          EPD.Clear(true);
          // Center the image on the display
          x = (DISPLAY_WIDTH - w) / 2;
          if (x < 0) x = 0;
          y = (DISPLAY_HEIGHT - h) / 2;
          if (y < 0) y = 0;
          iXOff = x; iYOff = y; // DEBUG
          //tif->setDrawParameters(1.0, TIFF_PIXEL_4BPP, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, ucTemp);
          tif->decode(x, y);
          tif->close();
          free(tif);
          EPD.UpdateFull(UPDATE_MODE_GC16);
          Serial.println("Finished tiff decode");
      }
    } // tif
    return 0;
} /* ViewFile() */

void lightSleep(uint64_t time_in_ms)
{
  esp_sleep_enable_timer_wakeup(time_in_ms * 1000L);
  esp_light_sleep_start();
}
void deepSleep(uint64_t time_in_ms) 
{
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL,         ESP_PD_OPTION_OFF);
  esp_sleep_enable_timer_wakeup(time_in_ms * 1000L); 
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");
    pinMode(M5PAPER_UP, INPUT);
    pinMode(M5PAPER_DOWN, INPUT);
    pinMode(M5PAPER_ENTER, INPUT);
    // Turn everything on
    EPD.Power(true); // turn on everything         
    EPD.begin(M5EPD_SCK_PIN, M5EPD_MOSI_PIN, M5EPD_MISO_PIN, M5EPD_CS_PIN, M5EPD_BUSY_PIN);
    EPD.SetRotation(00);
    EPD.Clear(true);
    if (bRotated) {
      obd.createVirtualDisplay(DISPLAY_HEIGHT, DISPLAY_WIDTH, ucBackBuffer);
      iDisplayWidth = DISPLAY_HEIGHT; iDisplayHeight = DISPLAY_WIDTH;
    } else {
      obd.createVirtualDisplay(DISPLAY_WIDTH, DISPLAY_HEIGHT, ucBackBuffer);
      iDisplayWidth = DISPLAY_WIDTH; iDisplayHeight = DISPLAY_HEIGHT;
    }
    obd.fillScreen(OBD_WHITE);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    while (!SD.begin(SD_CS, SPI, 20000000)) {
      Serial.println("Unable to access SD card");
      delay(1000);
    }
    Serial.println("SD card success");
} /* setup() */

void loop() {
char szDir[256], szName[256];

  strcpy(szDir, "/"); // this needs to be a non-const string because it will be modified in NavigateFiles()
  while (1) {
    int x, y, w, h, rc, bDone;
    int iOptions = 0, iScale = 1;
    NavigateFiles(szDir, szName);
    if (ViewFile(szName)) {
    } else {
      SG_WriteString(0, 0, (char *)"Error opening file", 0xf800, 0, 0);
      SG_WriteString(16, 16, (char *)"Press action key", 0xf800, 0, 0);
    }
    u32PrevBits = u32GPIOBits;
    bDone = 0;
    while (!bDone) {
      UpdateGPIOButtons();
      if (u32GPIOBits & RKEY_BUTT1 && !(u32PrevBits & RKEY_BUTT1)) {// newly pressed
         bDone = 1;
         ClearIdle();
      }
      u32PrevBits = u32GPIOBits;
      lightSleep(50); // save power while waiting for the user
      IncrementIdle();
    } // while waiting for key press
  } // while 1
} /* loop() */
