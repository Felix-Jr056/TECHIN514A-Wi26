# ErgoCompass
## Product Idea
ErgoCompass is a product that tracks how you sit using pressure sensors in the back pad and wirelessly sends the data to a desk display. The display converts your posture into a simple status on a physical gauge needle, with an LED alerts when you’ve been slouching for too long. A button on the display device allows calibrating and resets the session.

## Sensor Device
 The sensor device is composed of a fabric back pad with pressure sensors embedded in it. The sensors detect the pressure distribution when a user sits against the pad. The data from the sensors is collected by a microcontroller, which processes the information to determine the user's posture. The device is powered by a rechargeable battery and includes wireless communication capabilities to send the posture data to the desk display.

 Componernts used in the sensor device include:
 - 3 Pressure Sensors: 3 FSRs (Force Sensitive Resistor).
 - 1 Microcontroller: Custom PCB, may include FSR resistor network, status LED.
 - 1 Wireless Module: For transmitting data to the display device (BLE).
 - 1 Battery: Rechargeable LiPo battery with battery indicator.

## Display Device
The display device receives the posture data from the sensor device and translates it into a visual representation using a physical gauge needle. The needle moves to indicate whether the user's posture is good, moderate, or poor based on the pressure data received. Additionally, the display includes 3 LEDs that displays different colours serving as a pressure level indicator to correct user's posture. A button on the display allows users to calibrate the system and reset their session data.

Components used in the display device include:
- 1 Microcontroller: ESP32.
- 1 Stepper motor driven gauge needle: To visually represent the user's posture.
- 3 LEDs: Indicate the pressure level at each sensor.
- 1 Button: For calibration and session reset functionality.
 - 1 Battery: Small rechargeable LiPo battery.

## Diagrams
### Communication chart
![](src/Communicate_figure.png)


### System Block Diagram
![](src/System_block_diagram.png)