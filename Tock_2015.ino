/* SCROLL CLOCK
 * David Henshaw - November 2011-
 * v1. Basic timekeeping, LCD control, button logic - 11/6/11
 * v2. Infra-red detectors, StepGenie logic - 11/21/11
 * v3. Logic to handle index markers - 1/22/12
 * v4. Error and pause logic - 2/9/12
 * v5. Bug fix to force recalibrate on 1:00am rewind - 7/24/12
 ----
 * v6. Migrated to Visual Studio. Board = 'Arduino Duemilanove w/ ATmega328'
		Added new logic to cope with leader tape sticking due to duct tape glue
 */

#include <Wire.h>
#include <RealTimeClockDS1307.h> //https://github.com/davidhbrown/RealTimeClockDS1307
#include <LiquidCrystal_I2C.h>   //http://arduino-info.wikispaces.com/LCD-Blue-I2C

  //Variables:
  int targetIndex;                       //Index position we need to be at
  int currentIndex;                      //where we think we're at right now
  int prevIndexMinuteMarker;             //prior value of marker
  int prevIndexCalibrationMarker;        //prior value of marker
  int prevMinutes;                       //prior value of minutes
  int currentIndexMinuteMarker;          //value of analog pin
  int currentCalibrationMarker;          //value of analog pin
  char currentSection = ' ';             //are we
  char prevCurrentSection = ' ';         //       in white
  char currentCalibrationSection = ' ';  //                or black
  char prevCurrentCalibrationSection=' ';//                         territory?
  boolean errorStatus = false;           //for error trap  
  boolean pauseStatus = false;           //for pause feature    
  String line1 = "";                     //first  line of error message
  String line2 = "";                     //second line
  
  //Constants:
  const int addMinutePin = 2;            //Digital Pin 2 used for time switches
  const int subtractMinutePin = 3;       //Digital Pin 3
  const int indexMinuteMarker = 2;       //Analog Pin 2 - infrared minute marker
  const int indexCalibrationMarker = 3;  //Analog Pin 2 - infrared recalibration marker
  const int stepper1Step = 4;            //Digital Pins 4-9 - stepper motor control
  const int stepper1Direction = 5;
  const int stepper1Enable = 6;
  const int stepper2Step = 7;
  const int stepper2Direction = 8;
  const int stepper2Enable = 9;
  const int pausePin = 10;               //Digital Pin 10 used for pause/restart
  const int stepperDelay = 15;           //delay between step commands
  const int maxPulses = 400;             //number of stepper pulses before throwing error
  const int upperPinRead = 7;            //defines area where we are definately in white territory
  const int lowerPinRead = 4;            //definately in black territory
  
  LiquidCrystal_I2C lcd(0x27,16,2);      // set the LCD address to 0x27 for a 16 chars and 2 line display

void setup()  {
  Serial.begin(9600);
  lcd.init();                            // initialize the lcd 
  lcd.backlight();
  
  //Configure pins:
  pinMode(addMinutePin, INPUT);
  digitalWrite(addMinutePin, HIGH);     //turn on internal pull-up resistor
  pinMode(subtractMinutePin, INPUT);
  digitalWrite(subtractMinutePin, HIGH);//turn on internal pull-up resistor
  pinMode(pausePin, INPUT);
  digitalWrite(pausePin, HIGH);         //turn on internal pull-up resistor
  
  pinMode(stepper1Step, OUTPUT);       // step
  pinMode(stepper1Direction, OUTPUT);  // direction
  pinMode(stepper1Enable, OUTPUT);     // enable
  pinMode(stepper2Step, OUTPUT);       // step
  pinMode(stepper2Direction, OUTPUT);  // direction
  pinMode(stepper2Enable, OUTPUT);     // enable
  
  //Ensure both motors begin in disabled state
  digitalWrite(stepper1Enable, HIGH);    //disable motor   low = enabled, high = disabled
  digitalWrite(stepper2Enable, HIGH);    //disable motor   low = enabled, high = disabled
  
  /* //At startup: set time as 6:58 - TEST CODE ONLY
  RTC.setHours(6);
  RTC.setMinutes(58);
  RTC.setClock(); */ 

  //set currentindex as a large # to force rewind - PRODUCTION CODE ONLY
  targetIndex = 0;
  currentIndex = 200; 
  
  RTC.readClock();
  prevMinutes = 99;
}

void loop()
{
  if ((errorStatus != false) && (RTC.getSeconds() % 2) == 0) //if we're in error mode and seconds is an even number
  {      
     printErrorMessage();    
  }
  else
  {
    displayFullTime();
    displayPosition(); 
  }
  
   prevMinutes = RTC.getMinutes();
   
   if (digitalRead(pausePin) == LOW)
   {
     pauseStatus = !pauseStatus;
     displayFullTime();
     //Serial.println("pause");
     delay(5000);
   }
   else if (digitalRead(addMinutePin) == LOW)
   {
     errorStatus = false;
     addMinute();
     displayFullTime();
     delay(400);
   }
   else if (digitalRead(subtractMinutePin) == LOW) 
   {
     errorStatus = false;
     subtractMinute();
     displayFullTime();
     delay(400);     
   }
   else if ((targetIndex > currentIndex) && (errorStatus == false) && (pauseStatus == false))
   {
     moveForward();
   }
   else if ((targetIndex < currentIndex) && (errorStatus == false) && (pauseStatus == false))
   {
     moveBackward();
   }
   else
   {
     delay(990); //Wait almost a second before repeating loop   
   }
   
   RTC.readClock();
   calculateTargetIndex(); //Figure out target index 
}

//------------------------------------
void moveBackward(){ //go backward index # of times until current = target 
 digitalWrite(stepper2Direction, LOW); //direction low = ccw, High = cw
 digitalWrite(stepper2Enable, LOW);    //enable    low = enabled, high = disabled
 errorStatus = true;
 
 for (int i=currentIndex; i > targetIndex; i--)
 {
   for (int y=0; y < maxPulses; y++)
   {
   /*  Serial.print("MinuteMarker = ");
     Serial.print(analogRead(indexMinuteMarker));
     Serial.print(" & prev = ");
     Serial.println(prevIndexMinuteMarker); */
     
     prevCurrentSection = currentSection; //Store for next time round
     currentIndexMinuteMarker = analogRead(indexMinuteMarker);//get the value of the index minute marker at this exact time
     
     //determine if we are in the upper (white), lower (black) or indeterminate range (use previous value)
     if (currentIndexMinuteMarker >= upperPinRead)
     {
       currentSection = 'W'; //W=white B=black
     }
     else if (currentIndexMinuteMarker <= lowerPinRead)
     {
       currentSection = 'B'; //W=white B=black       
     }
     else
     {
       currentSection = prevCurrentSection; //Not sure where we are so use previous value
     }
     
     //Serial.print(currentIndexMinuteMarker);
     //Serial.println(currentSection);
     
    if (currentSection == 'B' && prevCurrentSection == 'W') //We were white, now we're black... we have seen the minute marker
   {
     //Serial.print("Bindex: ");
     //Serial.print(currentIndex-1);
     //Serial.print(" @ ");
     //Serial.println(y);
     errorStatus = false;
     break;
   }
   // prevIndexMinuteMarker = analogRead(indexMinuteMarker); //record value for comparison next time round 
                      
   // Check to see if we have reached the beginning of the reel
     prevCurrentCalibrationSection = currentCalibrationSection;     //Store for next time round
     currentCalibrationMarker = analogRead(indexCalibrationMarker); //get the value of the index minute marker at this exact time
     
     //determine if we are in the upper (white), lower (black) or indeterminate range (use previous value)
     if (currentCalibrationMarker >= upperPinRead)
     {
       currentCalibrationSection = 'W'; //W=white B=black
     }
     else if (currentCalibrationMarker <= lowerPinRead)
     {
       currentCalibrationSection = 'B'; //W=white B=black       
     }
     else
     {
       currentCalibrationSection = prevCurrentCalibrationSection; //Not sure where we are so use previous value
     }
 
   if (currentCalibrationSection == 'B' && prevCurrentCalibrationSection == 'W') //We were white, now we're black: we have seen the beginning marker
   {
     //Serial.print("@000");
     errorStatus = false;
     currentIndex = 1; //Will be reduced to zero after the break
     break;
   }
   
 if (digitalRead(pausePin) == LOW)
   {
     errorStatus = false;
     break;
   }
   
     digitalWrite(stepper2Step, HIGH);   // make one step
     delay(stepperDelay);                // pause for effect 
     digitalWrite(stepper2Step, LOW);    // reset step
     delay(stepperDelay);
      
   } //end of motor pulse loop
   
   //Serial.println(prevIndexMinuteMarker);
      currentIndex-- ;
      displayPosition();

 // Check to see if we have reached the beginning of the reel
   if (currentCalibrationSection == 'B') 
   {
     //Serial.println("!");
     break;
   } 
   
    if (digitalRead(pausePin) == LOW)
   {
     break;
   }
   
 //if this loop finishes and we haven't seen the minute marker, then there was a problem
   if (errorStatus == true){
    composeErrorMessage('R');  
    break;     
   }
 } //end of move backwards loop
 digitalWrite(stepper2Enable, HIGH);    //disable motor   low = enabled, high = disabled
}

//------------------------------------
void moveForward(){
 digitalWrite(stepper1Direction, HIGH); //direction low = ccw, High = cw
 digitalWrite(stepper1Enable, LOW);     //enable    low = enabled, high = disabled

 errorStatus = true;
 currentSection = ' ';

 for (int i=currentIndex; i < targetIndex; i++)
 {
   for (int y=0; y < maxPulses; y++)
   {
     prevCurrentSection = currentSection; //Store for next time round
     currentIndexMinuteMarker = analogRead(indexMinuteMarker);//get the value of the index minute marker at this exact time
     //Serial.print(currentIndexMinuteMarker);
     //Serial.print("-");
     
     //determine if we are in the upper (white), lower (black) or indeterminate range (use previous value)
     if (currentIndexMinuteMarker >= upperPinRead)
     {
       currentSection = 'W'; //W=white B=black
     }
     else if (currentIndexMinuteMarker <= lowerPinRead)
     {
       currentSection = 'B'; //W=white B=black       
     }
     else
     {
       currentSection = prevCurrentSection; //Not sure where we are so use previous value
     }

     //Serial.print(currentSection);
     //Serial.print("-");
     
    if (currentSection == 'B' && prevCurrentSection == 'W') //We were white, now we're black... we have seen the minute marker
   {
     //Serial.print("Findex: ");
     //Serial.print(currentIndex+1);
     //Serial.print(" @ ");
     //Serial.println(y);
     
     errorStatus = false;
     break;
   }

 if (digitalRead(pausePin) == LOW)
   {
     errorStatus = false;
     break;
   }
   
     digitalWrite(stepper1Step, HIGH);   // make one step
     delay(stepperDelay);                // pause for effect
     digitalWrite(stepper1Step, LOW);    // reset step
     delay(stepperDelay);
   }
   // Error-checking added 12/13/14. New power supply doesn't provide same power as previous one, and tape sometimes doesn't get past the "stickyness" of the duct tape markers
   byte attemptsMade = 0;	// counter to ensure we don't try edging forward forever

   //determine if we are in the upper (white), lower (black) or indeterminate range (use previous value)
   do { // double-check that we are seeing black
	   currentIndexMinuteMarker = analogRead(indexMinuteMarker);//get the value of the index minute marker at this exact time
	   // if not, the leader tape may have bounced back
	   // so try to advance forward until we see white:
	   digitalWrite(stepper1Step, HIGH);   // make one step
	   delay(stepperDelay);                // pause for effect
	   digitalWrite(stepper1Step, LOW);    // reset step
	   delay(stepperDelay);
	   attemptsMade++;
   } while ((attemptsMade < 100) && (currentIndexMinuteMarker >= upperPinRead));	// attempts to move forward < 100 OR in white zone
   
   currentIndex++ ;
   displayPosition();  
     // prevIndexMinuteMarker = analogRead(indexMinuteMarker); //record value for comparison next time round
      //Serial.println(prevIndexMinuteMarker);

 if (digitalRead(pausePin) == LOW)
   {
    break;
   }      
   
 //if this loop finishes and we haven't seen the minute marker, then there was a problem
   if (errorStatus == true){
    composeErrorMessage('F');  
    break;     
   }
 }
 digitalWrite(stepper1Enable, HIGH);    //disable motor   low = enabled, high = disabled
}

//------------------------------------
void calculateTargetIndex(){  //Given the current time, what index point on the reel should we go to?
  targetIndex = (RTC.getMinutes()/5) + 1;
  targetIndex = targetIndex + (RTC.getHours()-7) * 12;
 
 switch (RTC.getHours()) {  //special rule for 10pm thru 12:59am (end of reel) and 1am thru 6:59am (beginning of reel)
   case 22: //10:00pm
     targetIndex = 181;
     break;
   case 23:
     targetIndex = 181;
     break;
   case 0:
     targetIndex = 181;
     break;
   case 1:
     targetIndex = 0;
     if (RTC.getMinutes()== 0) currentIndex = 200; //force a recalibration at 1:00am in case a marker was skipped during the day
     break;
   case 2:
     targetIndex = 0;
     break;
   case 3:
     targetIndex = 0;
     break;
   case 4:
     targetIndex = 0;
     break;
   case 5:
     targetIndex = 0;
     break; 
   case 6:
     targetIndex = 0;
     break;
 } 
}

//------------------------------------
void displayFullTime(){
  
 if ((pauseStatus != false) && (RTC.getSeconds() % 2) == 0) {
   lcd.setCursor(5, 0);
   lcd.print(" PAUSED    ");
 }
 else if ((pauseStatus != false) || ((RTC.getMinutes() != prevMinutes) || (errorStatus != false))) {   //if minutes have changed, update full display
   lcd.setCursor(0, 0);
   lcd.print("Time");
   printLCDDigits(RTC.getHours()); 
   printLCDDigits(RTC.getMinutes());
   printLCDDigits(RTC.getSeconds());
   //for debug:   printLCDDigits(analogRead(indexMinuteMarker));
  }
  else {  //just print seconds
   lcd.setCursor(10, 0);
   printLCDDigits(RTC.getSeconds());
   //for debug:   printLCDDigits(analogRead(indexMinuteMarker));
  }
}

//------------------------------------
void displayPosition(){
   lcd.setCursor(0, 1);
   lcd.print("Mark:");
   printPositionDigits(targetIndex);
   lcd.print("/");
   printPositionDigits(currentIndex);
   lcd.print("       ");
}
//------------------------------------
void printPositionDigits(int digits){   // utility function for lcd display: prints leading 0's
  if(digits < 100) lcd.print('0');
  if(digits < 10) lcd.print('0');
  lcd.print(digits);
}
//------------------------------------
void printLCDDigits(int digits){   // utility function for lcd display: prints preceding colon and leading 0
  lcd.print(":");
  if(digits < 10) lcd.print('0');
  lcd.print(digits);
}

//------------------------------------
void composeErrorMessage(char reelDirection){  
  //      1234567890123456
  //Line1:ERROR F W/B 99
  //Line2:At HH:MM:SS
     errorStatus = true; 
     
     line1 = "ERROR " + String(reelDirection);
     line1 += " " + String(currentSection);
     line1 += "/" + String(prevCurrentSection);
     line1 += " " + String(currentIndexMinuteMarker);
     
     line2 = "At " + String(RTC.getHours());
     line2 += ":" + String(RTC.getMinutes());
     line2 += ":" + String(RTC.getSeconds());  
}

//------------------------------------
void printErrorMessage() {  
     lcd.clear();
     lcd.setCursor(0, 0);
     lcd.print(line1);  
     lcd.setCursor(0, 1);
     lcd.print(line2);   
     delay(6000);
     lcd.clear();  
}

//------------------------------------
void addMinute(){
    if (RTC.getMinutes() == 59)
    {
       RTC.setMinutes(0);
       if (RTC.getHours() == 23)
       {
         RTC.setHours(0);
       }
       else
       {
         RTC.setHours(RTC.getHours()+1);
       }
    }
    else
    {  
      RTC.setMinutes(RTC.getMinutes()+1);
    }
      RTC.setClock();
}

//------------------------------------
void subtractMinute(){
    if (RTC.getMinutes() == 0)
    {
       RTC.setMinutes(59);
       if (RTC.getHours() == 0)
       {
         RTC.setHours(23);
       }
       else
       {
         RTC.setHours(RTC.getHours()-1);
       }
    }
    else
    {  
      RTC.setMinutes(RTC.getMinutes()-1);
    }
      RTC.setClock();
}    
