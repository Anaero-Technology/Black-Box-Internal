### Serial Protocol

#### Info
Used to get the basic information about the device
Send:
```
info\n
```
Receive:
```
info [logging_state] [logging_file] [device_name] black-box [mac_address]\n
```
Logging_state will be either 0 or 1, if it is 0 logging _file will be 'none'

#### Type
Used to identify the black box and distinguish it from other Anaero API devices
Send:
```
type\n
```
Receive:
```
type black-box\n
```

#### Start
Used to begin logging and storing information
Send:
```
start [file_name]\n
``` 
File_name should lead with / and end with .txt
Receive:
Successfully started
```
done start\n
```
Failed, the sd card is not working
```
failed start nofiles\n
```
Failed, the file specified was already present
```
failed start alreadyexists\n
```
Failed, the device was already logging
```
already start\n
```

#### Stop
Used to end the logging
Send:
```
stop\n
```
Receive:
Successfully stopped
```
done stop\n
```
Failed, the sd card is not working
```
failed stop nofiles\n
```
Failed, the device was not loggin
```
already stop\n
```

#### Files
Get the sd card memory information and a list of the files stored on the sd card
Send:
```
files\n
``` 
Receive:
First the sd card memory information is reported
```
memory [total_sd_card_bytes] [used_sd_card_bytes]\n
```
Then the data files are sent
```
files start\n
```
For each file on the sd card
```
file [file_name] [file_size_bytes]\n
```
Then the sequence is finished
```
done files\n
```

#### Download
Used to download a file from the sd card
Send:
```
download [file_name] [bytes_to_download]\n
```
##### Repsonse sequence
Device begins
```
download start\n
```
For each line in the file
```
download [file_line]\n
```
Which should be responded to within 5 seconds with
```
next\n
```
Once the file is finished
```
download stop\n
```
If an error occurrs
Failed, response sequence not kept
```
download failed\n
```
Failed, the file did not exist
```
failed download nofile\n
```

#### Downlaod From
Used to download a file from the sd card from a certain event number onwards
Send:
```
downloadFrom [file_name] [event_number]\n
```
Response and sequence is identical to [download](#download)

#### Delete
Used to delete a file from the sd card
Send:
```
delete [file_name]\n
```
Receive:
Successfuly deleted
```
done delete\n
```
Failed, file does not exist
```
failed delete nofile\n
```
Failed, cannot delete while logging
```
already start\n
```

#### Get Time
Ued to get the current time from the real time clock
Send:
```
getTime\n
```
Receive:
```
time [time_stamp]\n
```
Time_stamp will be formatted 'yyyy mm dd hh mm ss'

#### Set Time
Set the time in the real time clock
Send:
```
setTime [time]\n
```
Time should be formatted as: year,month,day,hour,minute,second
Receive:
Successfully set
```
done setTime\n
```
Failed, currently logging so time cannot be changed
```
already start\n
```

#### Set Name
Used to set the name of the black box unit
Send:
```
setName [name]\n
```
Receive:
```
done setName\n
```

#### Get Hourly
Used to get the hourly tip count information
Send:
```
getHourly\n
```
Receive:
Start of the hourly data
```
tipfile start\n
```
Each line of the file containing the number of tips each hour
```
tipfile [file_line]\n
```
Each of these lines will be 15 numbers space separated, which are the tip counts for that hour, in order
Once the file has all been sent
```
tipfile done\n
```
Failure, sd card not working
```
getHourly failed nofiles\n
```

### Automatic Messages
Without a prompt the black box will report the following

#### Tip
Each tip that the device records will be sent
```
tip [file line]\n
```
The file line will be: Tip Number, Time Stamp, Seconds Elapsed, Channel Number, Temperature, Pressure

#### Hourly Tip Counts
Every hour the total tip counts for that hour will be sent
```
counts [c1] [c2] [c3] [c4] [c5] [c6] [c7] [c8] [c9] [c10] [c11] [c12] [c13] [c14] [c15]\n
```