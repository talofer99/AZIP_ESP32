/* Azip Arduino Zcode Interpreter Program
   --------------------------------------------------------------------
   Derived from John D. Holder's Jzip V2.1 source code
   http://jzip.sourceforge.net/
   --------------------------------------------------------------------
*/
/*
   Created by Louis Davis April, 2012
   Code update - adjusted to work with fanGL on ESP32 - by Tal Ofer April 2021
*/

// FABGL
#include "fabgl.h"
fabgl::VGA16Controller displayController;
fabgl::Terminal          Terminal;
fabgl::PS2Controller PS2Controller;

// SD
#include <SdFat.h>
#include <SPI.h>


// system
#include "ztypes.h"

// =========================================================================================
// TTGO Hardware Definition
// =========================================================================================
#define SS 13.
#define SDMHZ 25
#define SPIINIT 14,2,12,SS // TTGO_VGA32 (sck, miso, mosi, cs)
#define SDINIT SS, SD_SCK_MHZ(SDMHZ)

// file system
SdFat sd;
char gameFileName[15];

//used in process sd
byte lettersInRowCounter = 0;
// for last prompt
String lastPromptline = "";

void setup()
{
  // serial
  Serial.begin(115200);
  Serial.println("System Started");

  // display (R, G, B, H and V)
  displayController.begin(GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_4, GPIO_NUM_17, GPIO_NUM_15);
  // un comment this and comment the above to use the TTGO VGA32 settings.
  //displayController.begin();
  displayController.setResolution(VGA_640x480_60Hz);
  
  // ps2 control
  PS2Controller.begin(PS2Preset::KeyboardPort0);
  auto keyboard = PS2Controller.keyboard();

  // terminal
  Terminal.begin(&displayController);
  Terminal.connectLocally();
  Terminal.enableCursor(true);

  //Screen output
  delay(500);
  Terminal.write("\e[40;32m"); // background: black, foreground: green
  slowPrintf("Running System Tests\r\n");

  // KEYBOARD
  slowPrintf("Test Keyboard  : ");
  if (keyboard->isKeyboardAvailable())
    slowPrintf("[OK]\r\n");
  else
    slowPrintf("FAIL!\r\n");

  // SPI
  slowPrintf("Test SPI-Init  : ");
  SPI.begin(SPIINIT);
  slowPrintf("[OK]\r\n");

  // SD 
  slowPrintf("Test SD-Card   : ");
  if (sd.begin(SDINIT))
    slowPrintf("[OK]\r\n\r\n");
  else
    slowPrintf("FAIL!\r\n");


  Terminal.write("\e[40;97m"); // background: black, foreground: white

  //show list of games
  byte gameIndex = getGameSelection();
  getSelectedGameFileName(gameIndex, gameFileName);

  // open story
  slowPrintf("Loading Game....");
  open_story(gameFileName);

  // put your setup code here, to run once:
  configure( V1, V8 );

  initialize_screen(  );

  z_restart(  );

  Terminal.write("\e[40;97m"); // background: black, foreground: white
  Terminal.write("\e[2J");     // clear screen
  Terminal.write("\e[1;1H");   // move cursor to 1,1
}

void loop()
{
  // put your main code here, to run repeatedly:
  interpret( );
  // if we got to here the system was halted by Quit - we restart to allow choosing of new game. 
  ESP.restart();
}



/* *************************************
   Slow Print
 * *************************************/
void slowPrintf(const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  int size = vsnprintf(nullptr, 0, format, ap) + 1;
  if (size > 0) {
    va_end(ap);
    va_start(ap, format);
    char buf[size + 1];
    vsnprintf(buf, size, format, ap);
    for (int i = 0; i < size; ++i) {
      Terminal.write(buf[i]);
      delay(25);
    }
  }
  va_end(ap);
}


/* *************************************
   process output from sd to vga
 * *************************************/
void processreadfromsd(int c) {
  Serial.write((int) c);
  //(\n) new line && end of lettersInRowCounter with space
  if (c == 10 || (c == 32 && lettersInRowCounter >= 71))
  {
    Terminal.write('\r');
    Terminal.write('\n');
    //Serial.println(lettersInRowCounter);
    lettersInRowCounter = 0;
  }
  // not sure why 13 was used in palces but we ignore it
  // 91 & 93 are [] which comes aroudn system command
  else if (c == 13 || c == 91 || c == 93)
    // DO NOTHING
    Serial.println("IGNORE ....");
  else
  {
    lettersInRowCounter++;
    Terminal.write(c);
  }
}


/* *********************************************************
    Process kb
 * *********************************************************/

// Process prompt line
String processpromptline() {
  // set color to green
  Terminal.write("\e[32m");
  String returnPromptline = "";
  // read the next key
  char ckb = 0;
  // start do (while ckb != '/n')
  do  {
    // if KB avilable - process it
    if (Terminal.available()) {
      // we delay the GLCD so the read will be betetr
      ckb = Terminal.read();
      byte availableInTerminal;
      Serial.write((int) ckb);
      boolean addAndPrint = false;
      switch (ckb)
      {
        case 17: //scroll lock on
        case 19: //scroll lock off
        case 9: // tab
          //DO NOTHING --> THIS IS IGNORE LIST
          break;

        case 27: // ESC
          availableInTerminal = Terminal.available();
          if (availableInTerminal)
          {
            Serial.println("availableInTerminal = " + String(availableInTerminal));
            ckb = Terminal.read();
            Serial.print("NEXT READ IS - ");
            Serial.println((int) ckb);
            //command ([)
            if (ckb == 91)
            {
              Serial.println("YES It is a command ");
              for (uint8_t i = 1; i < availableInTerminal; i++)
              {
                ckb = Terminal.read();
                // its up arrow and nothing else was typed
                if (ckb == 65 && returnPromptline.length() == 0 && lastPromptline.length() > 0)
                {
                  Serial.println("UP ARROW");
                  returnPromptline = lastPromptline.substring(0, lastPromptline.length() - 1);
                  Terminal.write(returnPromptline.c_str());
                }//end if

              } //end for
            }
            // F1->F4
            else if (ckb == 79)
            {
              ckb = Terminal.read();
              Serial.print("F");
              Serial.println((byte) ckb - 79);
            }//end if
          }
          else
          {
            Serial.println("JUST ESCAPE!!");
          }//end if
          break;

        case 127: // back space
          // remove the last carecter typed
          if (returnPromptline.length() > 0)
          {
            // remove carecter from the prompt line
            returnPromptline = returnPromptline.substring(0, returnPromptline.length() - 1);
            // set the courser back
            Terminal.write("\b\e[K");
          }
          break;

        case 13: // enter
          addAndPrint = true;
          Terminal.write("\r\n");
          break;

        default: // all rest
          addAndPrint = true;
          break;
      } //end switch

      if (addAndPrint)
      {
        returnPromptline += ckb;
        Terminal.write(ckb);
      }


    }// end if
  } // end while
  while ( ckb != 13 );

  // set color back to white
  Terminal.write("\e[97m");

  // save last
  lastPromptline = returnPromptline;

  // return prompt line
  return returnPromptline;
} // end String


/******************************************************************
  / GET FILE NAME FROM FILE (only not dir and not MEMORY_FILE_NAME)
*******************************************************************/
boolean getFileName(SdFile *file, char * fileName, byte *fileNameLen)
{
  // get name & short name (no .DAT)
  *fileNameLen = file->getName(fileName, 15);
  if (!file->isDir() && memcmp(fileName, MEMORY_FILE_NAME, sizeof(MEMORY_FILE_NAME)))
    return true;
  else
    return false;
}



/******************************************************
  /   Get name of selected game by Index
* *****************************************************/

void getSelectedGameFileName(byte gameIndex, char * fileName)
{
  char fileNameInList[15];
  byte fileNameInListLen;
  byte fileCounter = 0;
  SdFile dir;
  SdFile file;
  // Open root directory
  if (!dir.open("/")) {
    Serial.println("dir.open failed");
    while (1);
  }
  // open each file
  while (file.openNext(&dir, O_RDONLY)) {

    // if not dir and not MEMORY_FILE_NAME
    if (getFileName(&file, fileNameInList, &fileNameInListLen))
    {
      fileCounter++;
      if (fileCounter == gameIndex)
        break;
    } //end if
    // close file
    file.close();
  } //end while

  // close directory
  dir.close();
  // copy to passed variable
  strncpy(fileName, fileNameInList, fileNameInListLen);
}


/* ************************************************
    Show list of games and get user selection
 * ************************************************/

byte getGameSelection()
{
  // get and show list of games
  slowPrintf("Available Games DAT Files:\n\r");
  // needed variables
  char fileNameInList[15];
  byte fileNameInListLen;
  byte fileCounter = 0;
  SdFile dir;
  SdFile file;
  // Open root directory
  if (!dir.open("/")) {
    Serial.println("dir.open failed");
    slowPrintf("Failed!!!  can not open root directory\n\r");
    while (1);
  }

  // open each file
  while (file.openNext(&dir, O_RDONLY)) {

    // if not dir and not MEMORY_FILE_NAME
    if (getFileName(&file, fileNameInList, &fileNameInListLen))
    {
      fileCounter++;
      Terminal.print(fileCounter);
      slowPrintf(") ");
      slowPrintf(fileNameInList);
      slowPrintf("\r\n");
    } //end if

    //close to move next
    file.close();
  } //end while

  // close directory
  dir.close();

  // define game selected
  byte gameSelected = 0;

  while (!gameSelected)
  {
    //show enter number
    slowPrintf("\r\nEnter Game Number : ");
    // get number
    gameSelected  = atoi(processpromptline().c_str());
    // if bigger then list or ZERO
    if (gameSelected > fileCounter || !gameSelected)
    {
      slowPrintf("Not Valid Option!\n\r");
      gameSelected = 0; //reset incase of bigger then
    } //end if
  } //end while

  return gameSelected;
} // end getGameSelection
