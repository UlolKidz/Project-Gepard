graph TD
  subgraph Power Matrix [WAGO Solderless Distribution]
    BATT[11.3V 3S Battery] -->|11.3V RAW| W_11V((11.3V Bus))
    W_11V -->|VM| TB[TB6612 Motor Driver]
    
    UNO_3V[UNO Q 3.3V Pin] -->|3.3V| W_3V((3.3V Logic Bus))
    W_3V -->|VCC| TB
    W_3V -->|VCC| HCSR04[HC-SR04 Ultrasonic]
    W_3V -->|VCC| IR[IR Sensors]
    
    BATT -->|GND| W_GND((Common GND Bus))
    UNO_GND[UNO Q GND] --> W_GND
    TB -->|GND| W_GND
  end

  subgraph Arduino UNO Q
    MCU[STM32U585 MCU]
    LINUX[QRB2210 Linux SBC]
    MCU <-->|Arduino_RouterBridge| LINUX
  end

  subgraph Sensors & Actuators
    TB -->|PWM / DIR| MOTORS[JGA25-370 Drive Motors]
    MCU -->|D4-D8, D12, D13| TB
    HCSR04 -->|TRIG A0 / ECHO A1| MCU
    IR -->|D9, D10, D11| MCU
  end

  subgraph Laptop Base Station
    GUI[Python GUI & Motion Detection]
    GUI <-->|UDP WiFi| LINUX
  end
