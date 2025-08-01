/*
* << AUTHOR >> 
*  Balazs Vecsey, vbstudio.hu
* 
* << CHIP >>
*  Processor: ATtiny85
*  Clock:     Internal 8 MHz
*
* << FUSES >>
*  CKDIV8    0   No clock division
*  CKSEL  0010   8MHz internal
*  SUT      10   Startup time
*  BOD     111   Brown out detection disabled to save power
*  
*  LFUSE: 62
*  HFUSE: DF
*  EFUSE: FF
*
*  avrdude.exe -c stk500v1 -p attiny85 -P com7 -U lfuse:r:-:i -v -C ..\etc\avrdude.conf -b 19200
*  avrdude.exe -c stk500v1 -p attiny85 -P com7 -U lfuse:w:0x62:m -U hfuse:w:0xdf:m -U efuse:w:0xff:m -v -C ..\etc\avrdude.conf -b 19200
*  
* << VERSION HISTORY >>
*  v1.0
*  - Initial version
*/

#include <avr/power.h>


// PINS
#define IO_CS 1
#define IO_DATA 0
#define IO_CLK A3
#define IO_BTN_DEC A1
#define IO_BTN_INC A2

// LED LOOK-UP-TABLE
// This is the relation between the output pins on the SN74HC595 and the LEDs.
// Each element sets which LEDs should be lit when the counter is at a certain number.
// One pin is used for the enable feedback, and should always be zero! (in this case that is the least significant bit)
//
// The LED index layout is as follows:
// |  1 |  2 |  3 |  4 |  5 |
// |  6 |  7 |  8 |  9 | 10 |
// | 11 | 12 | 13 | 14 | 15 |
//
#define UInt16(hi, lo) ((uint16_t)(hi << 8) | lo)
uint16_t dispMinutesLut[] =
{
    (uint16_t)0, // 0
    UInt16(B00000000, B00000010), // 1
    UInt16(B00000000, B00000110), // 2
    UInt16(B00000000, B00001110), // 3
    UInt16(B00000000, B00011110), // 4
    UInt16(B00000000, B00111110), // 5
    UInt16(B00000000, B01111110), // 6
    UInt16(B00000000, B11111110), // 7
    UInt16(B00000001, B11111110), // 8
    UInt16(B00000011, B11111110), // 9
    UInt16(B00000111, B11111110), // 10
    UInt16(B00001111, B11111110), // 11
    UInt16(B00011111, B11111110), // 12
    UInt16(B00111111, B11111110), // 13
    UInt16(B01111111, B11111110), // 14
    UInt16(B11111111, B11111110), // 15
};

// The pattern that shows when the timer is beeping. It's a mug :D
// | O O O O O |
// | O       O |
// |   O O O   |
uint16_t dispFlash  = UInt16(B01110100, B01111110);

// Milliseconds in one minute. Change it to calibrate for internal clock.
#define ONE_MINUTE 60000L


//
// CLOCK PRESCALER SETTERS,
// and corrected millis() for each
//
inline void setAlarmPrescaler()
{
    // Set clock prescaler
    CLKPR = _BV(CLKPCE); // Signal clock change
    CLKPR = _BV(CLKPS1) | _BV(CLKPS0); // 1 MHz (divide by 8)
}
#define alarmMillis() (millis() / 8)

inline void setCountDownPrescaler()
{
    // Set clock prescaler
    CLKPR = _BV(CLKPCE); // Signal clock change
    //CLKPR = _BV(CLKPS2) | _BV(CLKPS1); // 125 kHz (divide by 64)
    CLKPR = _BV(CLKPS2) | _BV(CLKPS1) | _BV(CLKPS0); // 62.5kHz (divide by 128)
    //CLKPR = _BV(CLKPS3); // 31.25kHz (divide by 256)
}
#define countDownMillis() (millis() * 2)

//
// HELPERS
//
inline bool readDecButton()
{
    return digitalRead(IO_BTN_DEC) == LOW;
}

inline bool readIncButton()
{
    return digitalRead(IO_BTN_INC) == LOW;
}

void updateDisplay(uint16_t data) // and no beeping
{
    digitalWrite(IO_CS, LOW);
    for (int i = 15; i >= 0; i--)
    {
        digitalWrite(IO_CLK, LOW);
        digitalWrite(IO_DATA, !!(data & (1 << i)));
        digitalWrite(IO_CLK, HIGH);
    }
    digitalWrite(IO_DATA, LOW); // Set this low, otherwise this would drive piezo after CS goes HIGH
    digitalWrite(IO_CS, HIGH);
}

void updateDisplayBeep(bool beep)
{
    uint16_t data = beep ? dispFlash : 0;
    digitalWrite(IO_CS, LOW);
    for (int i = 15; i >= 0; i--)
    {
        digitalWrite(IO_CLK, LOW);
        digitalWrite(IO_DATA, !!(data & (1 << i)));
        digitalWrite(IO_CLK, HIGH);
    }
    analogWrite(IO_DATA, beep ? 127 : 0);
    digitalWrite(IO_CS, HIGH);
}


//
// STATE
//
int _minutes = 5;


//
// INIT & START PROGRAM
//
void setup()
{
    pinMode(IO_DATA, OUTPUT);
    pinMode(IO_CLK, OUTPUT);
    pinMode(IO_CS, OUTPUT);
    pinMode(IO_BTN_DEC, INPUT);
    pinMode(IO_BTN_INC, INPUT);
    digitalWrite(IO_CS, LOW);

    // Turn off unused parts to conserve power (~0.5mA)
    ADCSRA = 0;        // Turn off the ADC subsystem
    ACSR |= _BV(ACD);  // Disable the analog comparator

    //power_timer0_disable();
    //power_timer1_disable();

    // Set Timer/Counter0 prescaler to 001 (no prescaling)
    TCCR0B = (TCCR0B & 0b11111000) | 0b001;

    powerOnAnim();
}

void loop()
{
    countDownLoop();
    alarmLoop();
}

//
// STARTUP ANIMATION
// Quickly counts up to 5
//
void powerOnAnim()
{
    setAlarmPrescaler();

    unsigned long startTime = alarmMillis();
    int lastNumber = 0;
    while (true)
    {
        int number = (alarmMillis() - startTime) / 30 + 1;
        if (number != lastNumber)
        {
            updateDisplay(dispMinutesLut[number]);
            if (number == 5)
            {
                return;
            }
        }
    }
}

//
// COUNT-DOWN STATE BEHAVIOR
//
void countDownLoop()
{
    setCountDownPrescaler();
    
    // Set initial state
    long minuteMillisecs = ONE_MINUTE;
    unsigned long lastTimestamp = countDownMillis(); // for delta time
    bool btnDecState = false;
    bool btnIncState = false;
    int lastMinutes = -1;

    while (true)
    {
        // Decrement trigger (rising edge)
        bool btnDec = readDecButton();
        if (btnDec && btnDec != btnDecState)
        {
            minuteMillisecs = ONE_MINUTE; // Reset seconds counter
            _minutes--;
            if (_minutes < 1)
            {
                _minutes = 15;
            }
        }
        btnDecState = btnDec;

        // Increment trigger (rising edge)
        bool btnInc = readIncButton();
        if (btnInc && btnInc != btnIncState)
        {
            minuteMillisecs = ONE_MINUTE; // Reset seconds counter
            _minutes++;
            if (_minutes > 15)
            {
                _minutes = 1;
            }
        }
        btnIncState = btnInc;

        // Get delta time
        unsigned long timestamp = countDownMillis();
        int delta = timestamp - lastTimestamp;
        lastTimestamp = timestamp;
        
        // Auto decrement minute
        minuteMillisecs -= delta;
        if (minuteMillisecs <= 0)
        {
            minuteMillisecs += ONE_MINUTE;
            _minutes--;
            if (_minutes <= 0)
            {
                updateDisplay(0);
                break; // Go to alarm
            }
        }
        
        // Update display
        if (_minutes != lastMinutes)
        {
            updateDisplay(dispMinutesLut[_minutes]);
            lastMinutes = _minutes;
        }
    }
}

//
// ALARM STATE BEHAVIOR
//
void alarmLoop()
{
    // Note: prescaler fails to set 1 out of 100 times.
    // - Possible cause #1: Sudden voltage drop caused by beeper and LEDs prevent new data from properly latching in.
    //   - Disproved, because even if I let it wait a second it's still happening.
    // - Solution that works: Call it multiple times, and then it won't miss for the second time.

    // Set prescaler
    unsigned long time;
    for (int i = 0; i < 2; i++)
    {
        setAlarmPrescaler();

        // Wait a bit
        time = alarmMillis();
        while (time == alarmMillis()) {};
    }

    // Set initial state
    time = alarmMillis(); // Start time
    int lastBeepState = false;
    int lastTimeframe = 0;

    while (true)
    {
        // Check button press for escape condition
        if (readDecButton())
        {
            _minutes = 15;
            break;
        }
        if (readIncButton())
        {
            _minutes = 1;
            break;
        }

        int timeframe = ((alarmMillis() - time) % 1000) / 100;

        // New cycle started, (skipping the first one)
        if (lastTimeframe != timeframe && timeframe == 0)
        {
            // Set prescaler again, just to be sure.
            setAlarmPrescaler();
        }

        // Turn speaker on and off
        bool beep = timeframe == 0 || timeframe == 2;
        if (beep != lastBeepState)
        {
            updateDisplayBeep(beep);
            lastBeepState = beep;
        }

        lastTimeframe = timeframe;
    }
    updateDisplayBeep(false);

    // Wait for button release
    while (true)
    {
        if (!readDecButton() && !readIncButton())
        {
            return;
        }
    }
}
