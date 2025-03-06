//ESP Data Logger and Communication
//
//For use with version 009 of the arduino GFM API System
//This version does not use the gas analyser
//WiFi code removed in this version
//Adjustments to UPDATE handling to fix issues with redownload, changed timing calculcations to correctly show times
//Added reporting of all tips for dynamic viewing, also uses UPDATE to ensure no data is lost
//  
//Written By Robbie Goldman, Last updated 1/11/2022

#include <FS.h>
#include <SD.h>
#include <RTClib.h>

//For S2 only
//#define arduinoRX 18
//#define arduinoTX 17

//If the data from the arduino should be recorded
bool collecting = false;
//The file location of the file currently being used to store information
char fileLocation[33];
//If the file system was setup successfully
bool filesWorking = false;

const int partLenMax = 19;
const int partMax = 8;
const int numberMessages = 5;
//Calculate maximum message length coming from arduino
const int messageLength = ((partLenMax + 1) * partMax) + 1;

//Create buffer to store message
char currentMessage[partMax][partLenMax + 1];
char collectionBuffer[messageLength];
int collectionBufferPosition = 0;
int currentMessagePosition = 0;
int currentMessageIndex = 0;

char currentArduinoMessage[160];
int currentAMPos = 0;

//Create buffer and pointer to store message from laptop
char commandMsg[97];
int commandPos = 0;

//Index number of next tip
uint32_t eventNumber = -1;

bool resettingArduino = false;
bool sentClear = false;

bool awaitingDownload = false;
bool awaitingHourly = false;
int downloadTimeout = 5;
bool awaitingResume = false;
char fileToDownload[33];  

uint32_t arduinoPauseTime = 0;
uint32_t espPauseTime = 0;

uint32_t experimentStartTime = 0;

uint32_t arduinoContactTime = 0;
uint32_t arduinoTimeoutDuration = 6000ul;

bool reRequesting = false;
uint32_t arduinoLastEventNumber = -1;

//Connection to real time clock
RTC_DS3231 rtc;

char myName[33];

int tipCounts[15];
const uint32_t hourLength = 60ul * 60ul * 1000ul;
const uint32_t ULONGMAX = 0UL - 1UL;
uint32_t hourStarted = 0ul;
const char hourlyTipFile[16] = "/hourlyTips.txt";

void setup() 
{
  //Initialize serial connections
  Serial.begin(115200);
  Serial2.begin(57600);
  //For S2 only
  //Serial1.begin(57600, SERIAL_8N1, arduinoRX, arduinoTX);

  //Start the file management
  bool startedSD = SD.begin();
  uint32_t cardType = CARD_NONE;
  if (startedSD){
    cardType = SD.cardType();
  }
  if (!startedSD || cardType == CARD_NONE){
    //If something went wrong display error messages
    Serial.println("Failed to start SD");
    Serial.println("Please check if card is present and connected properly.");
  }
  else{
    //Start the real time clock
    if (!rtc.begin()){
      //If something went wrong display error message
      Serial.println("Could not find real time clock");
    }else{
      //Flag to indicate file system and RTC started correctly
      filesWorking = true;
      //If the setup file is present
      if (SD.exists("/setup.txt")){
        //Open the setup file
        File setupFile = SD.open("/setup.txt", FILE_READ);
        //Read all the data out of it
        int numberChars = setupFile.available();
        char setupData[numberChars + 1];
        setupData[numberChars] = '\0';
        for (int i = 0; i < numberChars; i = i + 1){
          setupData[i] = setupFile.read();
        }
        //Close the file
        setupFile.close();

        //If there is a file storing the name of the device
        if (SD.exists("/name.txt")){
          //Read and store the name
          getName();
        }else{
          //Default name
          strcpy(myName, "Unnamed");
          setName();
        }
        
        //If the first character is a 1 - the experiment is running
        if (setupData[0] == '1'){
          if (SD.exists("/tipcount.txt")){
            File tipFile = SD.open("/tipcount.txt", FILE_READ);
            int numChar = tipFile.available();
            char tipFileMessage[numChar + 1];
            tipFileMessage[numChar] = '\0';
            for (int i = 0; i < numChar; i = i + 1){
              tipFileMessage[i] = tipFile.read();
            }
            tipFile.close();
            char *point;
            eventNumber = strtoul(tipFileMessage, &point, 10);
            if (eventNumber < 1){
              eventNumber = 1;
              configureTipFile();
            }
          }else{
            eventNumber = 1;
            configureTipFile();
          }
          
          int cha;
          //Iterate through remaining characters in the file
          for (cha = 2; cha < numberChars && cha < numberChars; cha = cha + 1){
            //Add the character to the file location
            fileLocation[cha - 2] = setupData[cha];
          }
          //Add null to end string
          fileLocation[cha - 2] = '\0';
  
          //If the file specified does not exist
          if (!SD.exists(fileLocation)){
            //Create the file and close it
            File newFile = SD.open(fileLocation, FILE_WRITE);
            newFile.close();
          }
          //Currently collecting data from the arduino
          collecting = true;
        }
        //Close the file
        setupFile.close();

        if (SD.exists("/time.txt")){
          File timeFile = SD.open("/time.txt", FILE_READ);
          int numChars = timeFile.available();
          char timeData[numChars + 1];
          int currentIndex = 0;
          for (int cha = 0; cha < numChars; cha = cha + 1){
            char nextCha = timeFile.read();
            timeData[currentIndex] = nextCha;
            currentIndex = currentIndex + 1;
          }
          timeFile.close();
          timeData[currentIndex] = '\0';
          experimentStartTime = strtol(timeData, NULL, 10);
        }else{
          experimentStartTime = 0UL;
        }
        Serial.write("Setup completed sucessfully\n");
        
      }else{
        //The file is not present so it is creating it in the off state
        Serial.println("No Setup File Found, performing fist time configuration");
        configureSetup(false);
      }
    }
  }
  
}

bool configureSetup(bool col){
  /*Change the state of the data collection*/
  //The default message to store into the memory (represents not running)
  char message[36];
  message[0] = '0';
  message[1] = '\0';
  //If it is supposed to be running
  if (col){
    //Change the message to the number one
    message[0] = '1';
    message[1] = ' ';
    bool done = false;
    //Iterate through characters in file location
    for (int cha = 0; cha < 34 and !done; cha = cha + 1){
      char next = fileLocation[cha];
      //Add each character to the file buffer
      message[2 + cha] = next;
      //Stop when the null character is reached
      if (next == '\0'){
        done = true;
      }
    }
  }

  //Boolean to store if the file was successfully updated
  bool updated = false;
  
  //If the file manager is running
  if (filesWorking){
    //Open the setup file
    File setupFile = SD.open("/setup.txt", FILE_WRITE);
    //Write the data to the file
    int bytesWritten = setupFile.print(message);
    //Check that the data was successfully written
    if (bytesWritten > 0){
      Serial.println("Setup successfully updated");
      updated = true;
    }else{
      //If no data was written - something went wrong
      Serial.write("Setup write failed\n");
    }
    //Close the file
    setupFile.close();
  }else{
    //If the file system was not working
    Serial.write("Setup files failed\n");
  }
  return updated;
}

void configureTipFile(){
  if (filesWorking){
    File tipFile = SD.open("/tipcount.txt", FILE_WRITE);
    char message[12];
    itoa(eventNumber, message, 10);
    tipFile.print(message);
    tipFile.close();
  }else{
    Serial.write("File System Has Failed (Updating tip number)");
  }
}

void getName(){
  if (filesWorking){
    if (SD.exists("/name.txt")){
      File nameFile = SD.open("/name.txt", FILE_READ);
      int index = 0;
      bool done = false;
      if (!nameFile.available()){
        done = true;
      }
      
      while (index < 32 && !done){
        char c = nameFile.read();
        if (c != '\0' && c != '\n'){
          myName[index] = c;
        }else{
          done = true;
        }
        index = index + 1;
        if (!nameFile.available()){
          done = true;
        }
      }
      
      if (index == 0){
        strcpy(myName, "Unnamed");
      }else{
        myName[index] = '\0';
      }

      nameFile.close();
    }
  }
}

void setName(){
  if (filesWorking){
    File nameFile = SD.open("/name.txt", FILE_WRITE);
    nameFile.println(myName);
    nameFile.close();
  }
}

void configureTime(){
  /*Set the start time to the current time so that it can be used to retrieve seconds since start*/
  if (filesWorking){
    //Set the start time
    experimentStartTime = getSecondsSince();
    //Message buffer
    char message[12];
    //Convert time to a c string
    ultoa(experimentStartTime, message, 10);
    //Open the time setup file
    File timeFile = SD.open("/time.txt", FILE_WRITE);
    //Store the time
    timeFile.print(message);
    //Close the file
    timeFile.close();
  }
}

void getTimeStamp(){
  /*Send the timestamp over the serial connection*/
  //Char buffer to hold timestamp
  char timeStamp[19];
  //Get the current time
  DateTime timeNow = rtc.now();
  //Store each of the time parts (largest to smallest) in array of integers
  int timeParts[6];
  timeParts[5] = timeNow.second();
  timeParts[4] = timeNow.minute();
  timeParts[3] = timeNow.hour();
  timeParts[2] = timeNow.day();
  timeParts[1] = timeNow.month();
  timeParts[0] = timeNow.year();

  //Buffer to hold current number
  char buff[5];
  int timePos = 0;

  //Iterate through each part
  for (int part = 0; part < 6; part = part + 1){
    //Convert to a c string
    itoa(timeParts[part], buff, 10);
    bool done = false;
    //Iterate through characters
    for (int ch = 0; ch < 5 and not done; ch = ch + 1){
      //If the end
      if (buff[ch] == '\0'){
        done = true;
      }else{
        //If still within the buffer
        if (timePos < 18){
          //Add character to the buffer
          timeStamp[timePos] = buff[ch];
          timePos = timePos + 1;
        }
      }
    }

    //Add a space if there are still more values to add
    if (part != 5 and timePos < 18){
      timeStamp[timePos] = ' ';
      timePos = timePos + 1;
    }
  }

  //Add terminator character
  timeStamp[timePos] = '\0';
  //Write message to serial
  Serial.write("time ");
  Serial.write(timeStamp);
  Serial.write("\n");
}

void setTimeStamp(int y, int m, int d, int h, int mi, int s){
  /*Set the current time on the real time clock using Year, Month, Day, Hours, MInutes and Seconds*/
  rtc.adjust(DateTime(y, m, d, h, mi, s));
}

uint32_t getSecondsSince(){
  /*Returns the number of seconds since the unix epoch*/
  if (filesWorking){
    uint32_t epochTime = rtc.now().unixtime();
    return epochTime;
  }

  return -1;
}

void getMemoryData(){
  /*Find how much memory is present on the board and how much has been used and output this information via serial*/
  if (filesWorking){
    //Get the total number of bytes available to the file system
    double totalBytes = SD.totalBytes();
    //Get the number of bytes being used to store information
    double usedBytes = SD.usedBytes();
    //Buffers used to convert the integers into c strings
    char totalBuff[65];
    char usedBuff[65];
    //Convert to c strings so that the numbers can be sent via serial
    int length1 = sprintf(totalBuff, "%.0f", totalBytes);
    int length2 = sprintf(usedBuff, "%.0f", usedBytes);

    //Send message
    Serial.write("memory ");
    Serial.write(totalBuff);
    Serial.write(" ");
    Serial.write(usedBuff);
    Serial.write("\n");
  }
}

void listFiles(){
  /*Outputs a list of files one line at a time preceded by the keyword: file*/
  //If the file system is working
  if (filesWorking){
    getMemoryData();
    char sizeBuff[33];
    Serial.write("file start\n");
    //Open the root directory
    File root = SD.open("/");
    //Open the first file
    File currentFile = root.openNextFile();
    while (currentFile){
      //If there is a file
      if (currentFile and !currentFile.isDirectory()){
        int fileSize = currentFile.size();
        itoa(fileSize, sizeBuff, 10);
        //Send the message to give the file name
        char fileName[33];
        strcpy(fileName, currentFile.name());
        Serial.write("file ");
        Serial.write(fileName);
        Serial.write(" ");
        Serial.write(sizeBuff);
        Serial.write("\n");
      }
      //Open the next file
      currentFile = root.openNextFile();
    }
    //Close the root location
    root.close();
  }
  //Send signal to indicate that it has finished (whether it was able to send files or not)
  Serial.write("done files\n");
}

bool fileNameSet(char fileName[33]){
  /*Set the file name to write data to*/
  //Check if the files system is working
  if (filesWorking){
    //If the file already exists
    if (SD.exists(fileName)){
      //Cannot start
      Serial.write("failed start alreadyexists\n");
      return false;
    }else{
      //Create the file
      File newFile = SD.open(fileName, FILE_WRITE);
      newFile.close();
      //Set the file location variable
      for (int ch = 0; ch < 33; ch = ch + 1){
        fileLocation[ch] = fileName[ch];
      }
      //Set file - can start
      return true;
    }
  }else{
    //Cannot start, the file system is not working
    Serial.write("failed start nofiles\n");
    return false;
  }
}

void readArduinoInput(){
  /*Read characters from the arduino and store them in a buffer*/
  //Repeat until there are no more characters - prioritises the arduino (may need to change to if)
  while (Serial2.available()){
    //Read the character
    char c = Serial2.read();
    //Serial.write(c);
    //If currently running an experiment
    if (collecting){
      //If the character is a new line - end of message
      if (c == '\n'){
        //Move the pointer to the end of the message (limited to end of array)
        currentAMPos = min(currentAMPos, 159);
        //Add null terminator
        currentArduinoMessage[currentAMPos] = '\0';
        //Convert from one message to multiple parts in 2D array
        splitToCurrentMessage();
        //Handle the message
        //Serial.write("Message received from Arduino\n");
        arduinoMessageReceived();
        //Reset the position
        currentAMPos = 0;
      }else{
        //If the message is not too long
        if (currentAMPos < 159){
          //Only useful characters allowed
          if (c > 31 && c < 123){
            //Add the character and increment the position
            currentArduinoMessage[currentAMPos] = c;
            currentAMPos = currentAMPos + 1;
          }
        }
      }
    }
  }
}

void splitToCurrentMessage(){
  /*Convert the single array message to a 2D array split on space*/
  //Not reached the end yet
  bool done = false;
  //Default starting positions
  currentMessageIndex = 0;
  currentMessagePosition = 0;

  //Iterate through characters in message
  for (int ch = 0; ch < 160 && !done; ch = ch + 1){
    //Get the character
    char c = currentArduinoMessage[ch];
    //If it is the terminator
    if (c == '\0'){
      //Stop processing characters
      done = true;
    }else{
      //If this is a space - separator of parts
      if (c == ' '){
        //Limit position to within part
        currentMessagePosition = min(currentMessagePosition, partLenMax);
        //Check that part and position are within range
        if (currentMessageIndex < partMax && currentMessagePosition <= partLenMax){
          //Add null terminator to part
          currentMessage[currentMessageIndex][currentMessagePosition] = '\0';
        }
        //Increment part counter
        currentMessageIndex = currentMessageIndex + 1;
        //Reset position counter
        currentMessagePosition = 0;
      }else{
        //If the part and position are within range of the array
        if(currentMessagePosition < partLenMax && currentMessageIndex < partMax){
          //Add the character to the array
          currentMessage[currentMessageIndex][currentMessagePosition] = c;
          //Increment position counter
          currentMessagePosition = currentMessagePosition + 1;
        }
      }
    }
  }

  //Iterate through all parts from the last one handled to the end
  for (int part = currentMessageIndex; part < partMax; part = part + 1){
    //Limit position to within range
    currentMessagePosition = min(currentMessagePosition, partLenMax);
    //Set the null terminator
    currentMessage[part][currentMessagePosition] = '\0';
    //Reset the position counter - ensures that first one will be after the message and all others will be empty
    currentMessagePosition = 0;
  }
}

void arduinoMessageReceived(){
  /*When a complete message has been recieved handle it correctly*/
  //Debug output via serial - so that it can be logged
  if (strcmp(currentMessage[1], "PING") != 0){
    for (int part = 0; part < partMax; part = part + 1){
      //If the part is not blank
      if (currentMessage[part] != ""){
        //Write the part to the file followed by a space
        Serial.write(currentMessage[part]);
        Serial.write(" ");
      }
    }
    Serial.write("\n");
  }

  //If needing to reset - for the start sequence
  if (resettingArduino){
    Serial.write("Testing Reset\n");
    //If the message just received was a ping and the clear has not yet been sent
    if (!sentClear && strcmp(currentMessage[1], "PING") == 0){
      //Send the clear message
      Serial2.write("SD_CLEAR\n");
      sentClear = true;
      Serial.write("Sent clear request\n");
    }

    if (sentClear){
      Serial.write("Waiting for clear response...\n");
    }

    //If the message indicates that the arduino is ready to reset
    if (strcmp(currentMessage[0], "READY") == 0){
      //Send the confirmation message
      Serial2.write("CONFIRM\n");
      Serial.write("Sent clear confirmation\n");
    }

    //If the message indicates that the reset is complete
    if (strcmp(currentMessage[0], "DONE") == 0){
      //Resetting is done
      resettingArduino = false;
      sentClear = false;
      //Start the experiment
      collecting = true;
      hourStarted = millis();
      Serial.write("done start\n");
      Serial2.write("LOGGING_ON\n");
    }
  }
  
  //If it is a data item and not waiting to reset
  if (!resettingArduino && strcmp(currentMessage[1], "DATA") == 0){ 
    //Flags to indicate if anthing was added and if anything has been added since the last part
    bool addedAnything = false;
    bool addedSince = false;
    //Iterate through the parts - ignore the first two
    for (int part = 4; part < 7 /*partMax*/; part = part + 1){
      //This part is not finished yet
      bool sectionDone = false;
      //If any text was added last part (prevents extra spaces in case of blank parts)
      if (addedSince){
        //Add a space and reset flag
        collectionBuffer[collectionBufferPosition] = ' ';
        collectionBufferPosition = collectionBufferPosition + 1;
        addedSince = false;
      }
      //Iterate through the characters
      for (int charPos = 0; charPos < partLenMax + 1; charPos = charPos + 1){
        //Get the character
        char character = currentMessage[part][charPos];
        //If this is the end of the part
        if (character == '\0'){
          //No need to add the remainder of the section
          sectionDone = true;
        }
        //If the section is sill being added
        if (!sectionDone){
          //Add the character to the buffer
          collectionBuffer[collectionBufferPosition] = currentMessage[part][charPos];
          collectionBufferPosition = collectionBufferPosition + 1;
          //Something has been added so set flags
          addedAnything = true;
          addedSince = true;
        }
      }
    }

    //If there was actually data in the message
    if (addedAnything){
      //Add a null to indicate this is the end of the buffer for now (do not increment counter so that next message starts here)
      collectionBuffer[collectionBufferPosition] = '\0';
      uint32_t eventTime = getSecondsSince() - experimentStartTime;
      uint32_t arduinoEventNumber = strtol(currentMessage[2], NULL, 10);
      uint32_t arduinoEventTime = strtol(currentMessage[3], NULL, 10);
      if (reRequesting){
        eventTime = arduinoEventTime;
      }

      bool askingAgain = false;
      
      if (eventNumber != arduinoEventNumber){
        if (reRequesting){
          eventNumber = arduinoEventNumber;
        }else{
          if (eventNumber == -1 || arduinoEventNumber < eventNumber){
            eventNumber = arduinoEventNumber;
          }else{
            //Store the end point
            arduinoLastEventNumber = arduinoEventNumber;
            //Request data dump from previous tip (give last one recieved)
            Serial2.write("DUMP_DATA_FROM ");
            Serial2.print(eventNumber);
            Serial2.write("\n");
            Serial.write("Re-Requesting tips from ");
            Serial.print(eventNumber);
            Serial.write("\n");
            askingAgain = true;
            reRequesting = true;
          }
        }
      }

      if (!askingAgain){
        //Write the line to the file
        outputCollectionBuffer(eventTime);
      }
    }
  }
  //If this is the message to say that the pause has begin
  if (strcmp(currentMessage[0], "DATA_PAUSED") == 0){
    //If a download has been scheduled
    if (awaitingDownload || awaitingHourly){
      //Get the pause time for both devices
      arduinoPauseTime = strtol(currentMessage[1], NULL, 10);
      espPauseTime = getSecondsSince();
      if (awaitingDownload){
        //Perform the download
        downloadFile();
      }else{
        sendHourTips();
      }
    }
    //Reset the download setup
    awaitingDownload = false;
    awaitingHourly = false;
    fileToDownload[0] = '\0';
    //Resume the arduino
    awaitingResume = true;
  }
  //If this is a restored data tip
  if (strcmp(currentMessage[0], "UPDATE") == 0 || strstr(currentMessage[0], "UPDATE") != NULL){
    //Serial.write("Added restored data\n");
    uint32_t eventTime = espPauseTime + (strtol(currentMessage[2], NULL, 10) - arduinoPauseTime) - experimentStartTime;
    //uint32_t eventTime = strtoul(currentMessage[2], NULL, 10);// - experimentStartTime;
    //Flags to indicate if anthing was added and if anything has been added since the last part
    bool addedAnything = false;
    bool addedSince = false;
    //Iterate through the parts - ignore the first two
    for (int part = 3; part < 6 /*partMax*/; part = part + 1){
      //This part is not finished yet
      bool sectionDone = false;
      //If any text was added last part (prevents extra spaces in case of blank parts)
      if (addedSince){
        //Add a space and reset flag
        collectionBuffer[collectionBufferPosition] = ' ';
        collectionBufferPosition = collectionBufferPosition + 1;
        addedSince = false;
      }
      //Iterate through the characters
      for (int charPos = 0; charPos < partLenMax + 1; charPos = charPos + 1){
        //Get the character
        char character = currentMessage[part][charPos];
        //If this is the end of the part
        if (character == '\0'){
          //No need to add the remainder of the section
          sectionDone = true;
        }
        //If the section is sill being added
        if (!sectionDone){
          //Add the character to the buffer
          collectionBuffer[collectionBufferPosition] = currentMessage[part][charPos];
          collectionBufferPosition = collectionBufferPosition + 1;
          //Something has been added so set flags
          addedAnything = true;
          addedSince = true;
        }
      }
    }

    //If there was actually data in the message
    if (addedAnything){
      //Add a null to indicate this is the end of the buffer for now (do not increment counter so that next message starts here)
      //Debug added U to indicate update line
      //collectionBuffer[collectionBufferPosition] = 'u';
      //collectionBufferPosition = collectionBufferPosition + 1;
      uint32_t arduinoEventNumber = atoi(currentMessage[1]);
      if (eventNumber != arduinoEventNumber){
        eventNumber = arduinoEventNumber;
      }
      collectionBuffer[collectionBufferPosition] = '\0';
      //Write the line to the file
      outputCollectionBuffer(eventTime);
    }
  }

  if (reRequesting && ((strcmp(currentMessage[0], "DONE") == 0) || (eventNumber > arduinoLastEventNumber && arduinoLastEventNumber > 0))){
    reRequesting = false;
    Serial.write("Stopped re-requesting\n");
  }

  //If waiting to resume data after a download
  if (awaitingResume){
    //If this message is a ping
    if (strcmp(currentMessage[1], "PING") == 0){
      //Resume the information from the arduino
      Serial2.write("RESUME_DATA\n");
      awaitingResume = false;
    }
  }
}

void checkArduinoTimeout(){
  if (resettingArduino){
    uint32_t currentTime = millis();
    uint32_t elapsed = currentTime - arduinoContactTime;
    if (currentTime < arduinoContactTime){
      elapsed = currentTime;
    }
    if (elapsed >= arduinoTimeoutDuration){
      Serial.write("failed start noarduino\n");
      collecting = false;
      resettingArduino = false;
      sentClear = false;
      fileLocation[0] = '\0';
      configureSetup(false);
      getMemoryData();
      listFiles();
    }
  }
}

void outputCollectionBuffer(uint32_t timeOccurred){
  /*Store the buffer and reset it*/
  Serial.write("Event time passed: ");
  Serial.print(timeOccurred);
  Serial.write('\n');
  //If ther file system is working and a file has been given to save in
  if (filesWorking && collecting && strcmp(fileLocation, "") != 0){
    //If the file does not exist
    if (!SD.exists(fileLocation)){
      //Create the file
      File newFile = SD.open(fileLocation, FILE_WRITE);
      newFile.close();
    }

    //Char array to store the whole message
    char writeBuffer[messageLength + 41];
    int writeBufferIndex = 0;
    
    //Get the time and convert to cstring
    uint32_t timeSince = timeOccurred;
    char timeBuffer[11];
    itoa(timeSince, timeBuffer, 10);
    //Convert the number of the event to a cstring
    char indexBuffer[11];
    itoa(eventNumber, indexBuffer, 10);

    char timeStampBuffer[19];
    DateTime timeNow = rtc.now();
    //If the time does not match current time - use the time occured (means that tips that are restored from updates do not have incorrect timestamp)
    if (timeOccurred != getSecondsSince() - experimentStartTime){
      timeNow = DateTime(timeOccurred + experimentStartTime);
    }
    int timeParts[6];
    timeParts[5] = timeNow.second();
    timeParts[4] = timeNow.minute();
    timeParts[3] = timeNow.hour();
    timeParts[2] = timeNow.day();
    timeParts[1] = timeNow.month();
    timeParts[0] = timeNow.year();

    bool done = false;
    //Iterate throug the characters in the event number
    for (int cha = 0; cha < 11 && !done; cha = cha + 1){
      char ch = indexBuffer[cha];
      //If it isn't the end of the numebr and the message isn't too long
      if (ch != '\0' and writeBufferIndex < messageLength + 40){
        //Add the character and increase the position
        writeBuffer[writeBufferIndex] = ch;
        writeBufferIndex = writeBufferIndex + 1;
      }else{
        //Stop writing the number
        done = true;
        //Add a space and increment the index
        writeBuffer[writeBufferIndex] = ' ';
        writeBufferIndex = writeBufferIndex + 1;
      }
    }
    
    char buff[5];
    int timePos = 0;

    //Iterate through the different parts of the time
    for (int part = 0; part < 6; part = part + 1){
      //Convert to a c string in the buffer
      itoa(timeParts[part], buff, 10);
      bool done = false;
      //Iterate through characters in the buffer
      for (int ch = 0; ch < 5 and not done; ch = ch + 1){
        //If the end has been reached
        if (buff[ch] == '\0'){
          //Stop
          done = true;
        }else{
          //If the end of the buffer has not been reached
          if (timePos < 18){
            //Add the character and increment the position
            timeStampBuffer[timePos] = buff[ch];
            timePos = timePos + 1;
          }
        }
      }

      //If this is not the last part and the buffer is not full
      if (part != 5 and timePos < 18){
        //Add the delimeter between parts
        timeStampBuffer[timePos] = '.';
        timePos = timePos + 1;
      }
    }
    //Add terminator character to buffer
    timeStampBuffer[timePos] = '\0';

    done = false;
    //Iterate through each character in the time stamp
    for (int cha = 0; cha < 19 && !done; cha = cha + 1){
      char ch = timeStampBuffer[cha];
      //If this is not the end of the number and the message is not too long
      if (ch != '\0' and writeBufferIndex < messageLength + 40){
        //Add the character and increment the index
        writeBuffer[writeBufferIndex] = ch;
        writeBufferIndex = writeBufferIndex + 1;
      }else{
        //Stop writing the number
        done = true;
        //Add a space and increment the index
        writeBuffer[writeBufferIndex] = ' ';
        writeBufferIndex = writeBufferIndex + 1;
      }
    }
    
    done = false;
    //Iterate through each character in the time
    for (int cha = 0; cha < 11 && !done; cha = cha + 1){
      char ch = timeBuffer[cha];
      //If this is not the end of the number and the message is not too long
      if (ch != '\0' and writeBufferIndex < messageLength + 40){
        //Add the character and increment the index
        writeBuffer[writeBufferIndex] = ch;
        writeBufferIndex = writeBufferIndex + 1;
      }else{
        //Stop writing the number
        done = true;
        //Add a space and increment the index
        writeBuffer[writeBufferIndex] = ' ';
        writeBufferIndex = writeBufferIndex + 1;
      }
    }
    char channel[4];
    int channelPos = 0;
    done = false;
    for (int cha = 0; cha < messageLength && !done; cha = cha + 1){
      char c = collectionBuffer[cha];
      if(c != ' '){
        if(channelPos < 3){
          channel[channelPos] = c;
          channelPos = channelPos + 1;
        }
      }else{
        done = true;
      }
    }
    channel[channelPos] = '\0';
    int channelNumber = atoi(channel);
    if(channelNumber > 0 && channelNumber < 16){
      tipCounts[channelNumber - 1] = tipCounts[channelNumber - 1] + 1;
    }
    
    done = false;
    //Iterate through the characters in the message
    for (int cha = 0; cha < messageLength && !done; cha = cha + 1){
      char ch = collectionBuffer[cha];
      //If this is not the end of the message and it is not too long
      if (ch != '\0' and writeBufferIndex < messageLength + 40){
        //Add the character and increment the index
        writeBuffer[writeBufferIndex] = ch;
        writeBufferIndex = writeBufferIndex + 1;
      }else{
        //Stop writing the message
        done = true;
      }
    }

    //If the index is less than or at the end of the message
    if (writeBufferIndex < messageLength + 41){
      //Add a null at the end of the message
      writeBuffer[writeBufferIndex] = '\0';
    }else{
      //Add a null at the very last index - prevents extra data being read
      writeBuffer[messageLength + 40] = '\0';
    }
    
    //Open the file for append
    File appendFile = SD.open(fileLocation, FILE_APPEND);
    //Add the contents of the buffer (with a new line at the end)
    appendFile.print(writeBuffer);
    appendFile.print("\n");
    //Close the file to update the data
    appendFile.close();
    writeBuffer[0] = '\0';
    writeBufferIndex = 0;
    eventNumber = eventNumber + 1;
    configureTipFile();
  }else{
    if (!filesWorking){
      Serial.write("File System Has Failed (Attempting to write line)\n");
    }
  }
  //Reset the buffer position
  collectionBuffer[0] = '\0';
  collectionBufferPosition = 0;
}

void downloadFile(){
  /*Send the file as a download via serial then resume the Arduino communication*/
  if (SD.exists(fileToDownload)){
    char lastChar;
    //Open the file to send
    File fileToSend = SD.open(fileToDownload, FILE_READ);
    //Get the number of characters in the file
    int charNumber = fileToSend.available();
    char charNumberBuffer[33];
    itoa(charNumber, charNumberBuffer, 10);
    //Send message to indicate that the file is about to be transferred, with the file name
    Serial.write("download start ");
    Serial.write(fileToDownload);
    Serial.write(" ");
    Serial.write(charNumberBuffer);
    Serial.write("\n");

    uint32_t lastTime = getSecondsSince();
    bool terminated = false;
    //If there is data to send
    if (charNumber > 0){
      //keyword to indicate a line is being sent
      Serial.write("download ");
    }
    //Iterate through each character index
    for (int cha = 0; cha < charNumber && !terminated; cha = cha + 1){
      //Read the character
      char nextChar = fileToSend.read();
      char next[2] = {nextChar, '\0'};
      //Write the character
      Serial.write(next);
      //If this is the end of a line
      if (nextChar == '\n' && cha != charNumber - 1){
        //Message received to indicate that it is ready for the next item
        char waitMessage[64];
        int waitIndex = 0;
        bool waiting = true;
        //Repeat until no longer waiting for a response
        while (waiting){
          //Get the current time
          uint32_t currentTime = getSecondsSince();
          //If the timeout is exceeded
          if (currentTime - lastTime > downloadTimeout){
            //Stop waiting - failed, terminate the data transfer
            waiting = false;
            terminated = true;
          }

          //While there is data to read from the arduino (can safely be ignored)
          while (Serial2.available()){
            //Read the characters until the buffer is empty
            Serial2.read();
          }

          //While there are characters to read from python
          while (Serial.available()){
            //Reacd character
            char msgChar = '\0';
            msgChar = Serial.read();
            //If it is a new line
            if (msgChar == '\n'){
              //If the message is 'next' - to indicate that it is ready for the next item
              if (strcmp(waitMessage, "next") == 0){
                //No longer waiting, update the time
                waiting = false;
                lastTime = currentTime;
              }
              //Reset message and index
              waitMessage[0] = '\0';
              waitIndex = 0;
            //If it is any other character and within the message length
            }else if (waitIndex < 63){
              //Add the character
              waitMessage[waitIndex] = msgChar;
              //Add termination character and increment counter
              waitMessage[waitIndex + 1] = '\0';
              waitIndex = waitIndex + 1;
            }
          }
        }

        //If the download did not time out
        if (!terminated){
          //Keyword to indicate the next line
          Serial.write("download ");
        }
      }
      //Store the final character that was sent
      lastChar = nextChar;
    }

    //If the file did not terminate with a newline send one to terminate the command (and there was something to send)
    if (lastChar != '\n' && charNumber > 0 && !terminated){
      Serial.write("\n");
    }

    //Close the file
    fileToSend.close();
    if (!terminated){
      //Send message to indicate that file has been downloaded fully
      Serial.write("download stop\n");
    }else{
      //Send message to indicate that something went wrong
      Serial.write("download failed\n");
    }
    
  }else{
    //Message to indicate that the requested file does not exist
    Serial.write("failed download nofile\n");
  }
}

void resetTipCounters(){
  //Reset the counters for each channel so a new hour can begin
  for (int i = 0; i < 15; i = i + 1){
    tipCounts[i] = 0;
  }
}

void updateHourTime(){
  if(collecting){
    uint32_t currentTime = millis();
    uint32_t elapsed = currentTime - hourStarted;
    if (currentTime < hourStarted){
      elapsed = (ULONGMAX - hourStarted) + currentTime;
    }
    if (elapsed >= hourLength){
      outputTipCounters();
      if(filesWorking){
        saveHourTips();
      }
      resetTipCounters();
      hourStarted = hourStarted + hourLength;
    }
  }
}

void outputTipCounters(){
  Serial.write("counts ");
  for (int i = 0; i < 15; i = i + 1){
    Serial.print(tipCounts[i]);
    Serial.write(" ");  
  }
  Serial.write("\n");
}

void readCommandInput(){
  //If there is data to be read from the laptop end
  if (Serial.available()){
    //How many characters are incoming
    int numberCharacters = Serial.available();
  
    //Iterate through the characters
    for(int ch = 0; ch < numberCharacters; ch = ch + 1){
      //Read the next character
      char newChar = Serial.read();
      //If the end of the current message has not been reached
      if (newChar != '\n'){
        //If the message is not too long
        if (commandPos < 96){
          //Add the character to the message
          commandMsg[commandPos] = newChar;
          commandPos = commandPos + 1;
          if (commandPos > 96){
            commandPos = 96;
          }
        }
      }else{
        commandMsg[commandPos] = '\0';
        //Array to store the message split into three parts (space separated)
        char msgParts[3][33];
        msgParts[0][0] = '\0';
        msgParts[1][0] = '\0';
        msgParts[2][0] = '\0';
        
        //Which is the current part
        int part = 0;
        //Current character index (within the part)
        int cha = 0;
        //Iterate through characters within the message
        for (int charIndex = 0; charIndex < 97; charIndex = charIndex + 1){
          //If a space is found and this is the first part and some letters have been added
          if (commandMsg[charIndex] == ' ' and part < 2 and cha != 0){
            //Add null to end the part
            msgParts[part][cha] = '\0';
            //Move to start of next part
            cha = 0;
            part = part + 1;
          }else{
            //If the character is in range (truncates extra characters)
            if (cha < 32){
              //Add character to message part
              msgParts[part][cha] = commandMsg[charIndex];
              cha = cha + 1;
            }
          }
        }

        //Process the received message
        handleCommandInput(msgParts);
        //Reset the input
        for (int i = 0; i < 97; i = i + 1){
          commandMsg[i] = '\0';
        }
        commandPos = 0;
        
      }
    }

  }
}

void handleCommandInput(char msgParts[3][33]){
  /*Handle the command sent from serial and perform the correct action and respond appropriately*/
  //If this is a call for the information
  if (strcmp(msgParts[0], "info") == 0){
    //Send back information regarding the collection state
     if (collecting){
       //Is collecting and the file name
       Serial.write("info 1 ");
       Serial.write(fileLocation);
       Serial.write(" ");
     }else{
       //Not collecting
       Serial.write("info 0 none ");
     }
     Serial.write(myName);
     Serial.write("\n");
     //Send the information regarding the memory usage
     //getMemoryData();
  }
  //If this is the command to start recieving data
  if (strcmp(msgParts[0], "start") == 0){
    //If not currently running the experiment
    if (!collecting){
      //handle file name - check character length and validity (28 characters max) and that it doesn't exist yet
      bool success = fileNameSet(msgParts[1]);

      if (success){
        bool started = configureSetup(true);
        if (started){
          configureTime();
          //Reset message input buffer
          for (int i = 0; i < partMax; i = i + 1){
            currentMessage[i][0] = '\0';
          }
          //Reset counters and file
          resetTipCounters();
          clearHourTips();
          //Reset the event index and the collection buffer
          eventNumber = 1;
          configureTipFile();
          collectionBuffer[0] = '\0';
          collectionBufferPosition = 0;
          resettingArduino = true;
          arduinoContactTime = millis();
          sentClear = false;
          awaitingResume = false;
          //Flag set to start collecting arduino data
          collecting = true;
          Serial.write("Start working - awaiting reset\n");
        }else{
          //File system or write failed, send message to indicate this
          Serial.write("failed start nofiles\n");
        }
      }
    }else{
      //Message to indicate that the experiment was already running
      Serial.write("already start\n");
    }
  }

  //If this is the command to stop receiving data
  if (strcmp(msgParts[0], "stop") == 0){
    if (collecting){
      bool stopped = configureSetup(false);
      if (stopped){
        //Flag set to stop collecting arduino data
        collecting = false;
        //Reset the file destination
        fileLocation[0] = '\0';
        //Send signal to indicate that the stop was perfomed successfully
        Serial.write("done stop\n");
        Serial2.write("LOGGING_OFF\n");
      }else{
        //Message to inicate that it did not stop due to a file system issue
        Serial.write("failed stop nofiles\n");
      }
    }else{
      //Message to indicate that the experiment was not running already
      Serial.write("already stop\n");
    }
    //Send data about memory usage
    getMemoryData();
  }

  //If this is the command to send the file list
  if (strcmp(msgParts[0], "files") == 0){
    //Send back the list of files
    listFiles();
  }

  //If this is the command to delete a file
  if (strcmp(msgParts[0], "delete") == 0){
    //If not currently running
    if (!collecting){
      //If there is a file with the given name
      if (SD.exists(msgParts[1])){
        //Delete the file
        SD.remove(msgParts[1]);
        //Send signal that file was removed successfully
        Serial.write("done delete\n");
      }else{
        //Send signal that the file did not exist
        Serial.write("failed delete nofile\n");  
      }
    }else{
      //Send signal that the system is currently running
      Serial.write("already start\n");
    }
  }

  //If the message requests a file download
  if (strcmp(msgParts[0], "download") == 0){
    bool done = false;
    //Iterate through characters in the file name
    for (int ch = 0; ch < 33 && !done; ch = ch + 1){
      //Add the character to the name of the file to be downloaded
      fileToDownload[ch] = msgParts[1][ch];
      //Once the end of the name has been reached
      if (msgParts[1][ch] == '\0'){
        done = true;
      }
    }

    //If currently receiving data
    if (collecting){
      //Perform a pause first
      awaitingDownload = true;
      Serial2.write("PAUSE_DATA\n");
    }else{
      //Start the file download
      downloadFile();
    }
    
  }

  //If this is a request to get the RTC time
  if (strcmp(msgParts[0], "getTime") == 0){
    getTimeStamp();
  }

  //If this is a request to set the RTC Ttime
  if (strcmp(msgParts[0], "setTime") == 0){
    //If not currently running an experiment
    if (!collecting){
      //Buffer to store the values
      char timeValues[6][5];
      //Array to store the integer version of the values
      int timeValuesInt[6];
  
      bool done = false;
      int value = 0;
      int index = 0;
      //Iterate through the characters in the message
      for (int cha = 0; cha < 33 or not done; cha = cha + 1){
        //If the end has been reached
        if (msgParts[1][cha] == '\0'){
          done = true;
        }else{
          //If this is a separator
          if (msgParts[1][cha] == ','){
            //Add terminator to the value
            timeValues[value][index] = '\0';
            //Move on to next value if there is one
            if (value < 5){
              value = value + 1;
              index = 0;
            }else{
              //If there is not a next value - finished
              done = true;
            }
          }else{
            //Add the character to the value
            timeValues[value][index] = msgParts[1][cha];
            //Move to next position
            index = index + 1;
            //Limit to prevent out of range
            if (index > 4){
              index = 4;
            }
          }
        }
      }

      //Add terminator character to all following values
      timeValues[value][index] = '\0';
      for (int v = value + 1; v < 6; v = v + 1){
        timeValues[v][0] = '\0';
      }

      //Iterate through and convert to integers
      for (int i = 0; i < 6; i = i + 1){
        timeValuesInt[i] = atoi(timeValues[i]);
      }

      //Set the time from the values
      setTimeStamp(timeValuesInt[0], timeValuesInt[1], timeValuesInt[2], timeValuesInt[3], timeValuesInt[4], timeValuesInt[5]);

      //Send message to indicate successful time change
      Serial.write("done setTime\n");
    }else{
      //Send message to indicate that the experiment is currently running
      Serial.write("already start\n");
    }
  }

  if (strcmp(msgParts[0], "setName") == 0){
    bool done = false;
    int index = 0;
    int nameIndex = 0;
    while (index < 32 && !done){
      char c = msgParts[1][index];
      index = index + 1;
      if (c == '\0' || c == '\n'){
        done = true;
      }else{
        myName[nameIndex] = c;
        nameIndex = nameIndex + 1;
      }
    }
    myName[nameIndex] = '\0';
    setName();
  }

  if (strcmp(msgParts[0], "getHourly") == 0){
    if(filesWorking){
      //If currently receiving data
      if (collecting){
        //Perform a pause first
        awaitingHourly = true;
        Serial2.write("PAUSE_DATA\n");
      }else{
        //Start the file download
        sendHourTips();
      }
    }else{
      Serial.write("getHourly failed nofiles\n");
    }
  }
}

void saveHourTips(){
  if(!SD.exists(hourlyTipFile)){
    File hourlyFile = SD.open(hourlyTipFile, FILE_WRITE);
    hourlyFile.close();
  }
  File hourlyFile = SD.open(hourlyTipFile, FILE_APPEND);
  for(int i = 0; i < 15; i = i + 1){
    hourlyFile.print(tipCounts[i]);
    if(i != 14){
      hourlyFile.print(' ');
    }else{
      hourlyFile.print('\n');
    }
  }
  hourlyFile.close();
}

void clearHourTips(){
  File hourlyFile = SD.open(hourlyTipFile, FILE_WRITE);
  hourlyFile.close();
}

void sendHourTips(){
  Serial.write("tipfile start\n");
  if(SD.exists(hourlyTipFile)){
    File hourlyFile = SD.open(hourlyTipFile, FILE_READ);
    bool newLine = true;
    while(hourlyFile.available()){
      if(newLine){
        Serial.write("tipfile ");
        newLine = false;
      }
      char c = hourlyFile.read();
      Serial.write(c);
      if(c == '\n'){
        newLine = true;
      }
    }
    if(!newLine){
      Serial.write('\n');
    }
  }
  Serial.write("tipfile done\n");
}

void loop()
{
  //Get in any availiable data from the arduino
  readArduinoInput();
  //Get in any availiable data from the laptop
  readCommandInput();
  if(resettingArduino){
    checkArduinoTimeout();
  }
  updateHourTime();
}
