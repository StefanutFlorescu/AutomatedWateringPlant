# AutomatedWateringPlant
## Description
In this project I am going to build a full watering system for small plants that will use a microcontroller to read parameters and decide if the plants needs water or not
I am going to read the sensors and then send the values to a more powerful device(a raspberry pi zero that i own). Of course on the "more powerful device" i want to integrate a simple CNN that I will build. Beside that I want to implement a simple UI app(probably a web app) for the phone to control the system.

## Bill of materials
- Esp32(I already have an Esp8266 and I kind of want to use it)
- Raspberry pi zero(I will use it either connected directly to the microcontroller, or make it a simple server and connect to it via phone)
- Electrovalve
- Humidity Sensor
- Light Sensor
- Temperature Sensor
- Pot
- Dirt
- Flower
  
I don't plan to follow any tutorials.

## Questions
### Q1 - What is the system boundary?
The microcontroller (Esp32 or Esp8266) will control the valve and gather the sensor information and will send them to a raspberry pi server where I will process the information. Also there is going to be a nice UI on the phone to manually control the system and to activate the automation.
### Q2 - Where does intelligence live?
The collecting sensor part and the action(opening the electrovale) will be taken by the microcontroller, but the CNN will be on the Raspberry pi. So probabbly it is right to say that the intelligence is more on the raspberry pi.
### Q3 - What is the hardest technical problem?
The main problem is the fact that the electrovalve works with 12V and the microcontroller only outputs 5v. The second one is of course building the CNN.
### Q4 - What is the minimum demo?
A flower pot where i can open/close the valve and the water flows in. I control the actions from my phone app where I can see different things about the plant. Also I can press a button where everything is fully automated and I don't need to do anything again.
### Q5 - Why is this not just a tutorial?
Beside the simple action of opening/closing the valve, which is also not that simple because at this moment I am not 100% sure how I am going to give it 12V, I will build a simple app and the CNN part. This is probbably a good product to sale and this is an MVP. I am 100% sure I can then sell the product to my family.
### Q6 - Do I neet an ESP32
If I am not allowed to use my ESP8266, then the answer is YES. If I can use my own microcontroller then NO.

Diagram:
[Sensors] → [ESP] → [Raspberry Pi] → [ESP] → [Valve]
                          ↑
                       [Phone App]
