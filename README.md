# Control Software for Black Box
This is the software compatible with Anaero Technology's Black Box data logger.
The software here is designed to run on the ESP32 within the black box itself and allows for communication with a computer using a USB cable and the software found here: https://github.com/Anaero-Technology/Black-Box

## How to use this
In order to install the software you will need the Arduino IDE which can be found here: https://www.arduino.cc/en/software
### Setup
Under 'Tools', 'Board' and 'Board Manager' make sure you have 'Arduino ESP32 Boards' installed.
### Uploading
Connect the Black Box to your computer using a USB cable.
On the board there are two buttons, one marked 'boot' and the other 'reset'. Press and hold boot, then press and release reset followed by releasing boot, you may need to open the box of the logger in order to access these buttons.
You should then see under 'Tools' and 'Port' that a port has become available, select it. If there is not a port check your cable is working and try connecting again.
Under 'Tools' and 'Board' select 'ESP32 Dev Module' which should be under 'ESP32 Arduino'.
Then press the upload button (or ctrl+u) and wait for the code to be transferred.

Then unplug the device fully to allow it to restart, then attempt to connect with the Python interface.
