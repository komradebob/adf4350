// ADF4350 -> 1.000 GHz, lowest RF output power, reports lock status
// Oak pin map: P5=LD(in), P6=LE, P7=MOSI(DAT), P9=SCLK(CLK), P10=MUX, GND
// CE must be tied HIGH; PDR pins tied HIGH.

const int LD_PIN   = P5;
const int LE_PIN   = P6;
const int MOSI_PIN = P7;
const int SCLK_PIN = P9;
const int MUX_PIN  = P10;

// R0: INT << 15 | FRAC << 3 | 000
uint32_t makeR0(uint16_t INT, uint16_t FRAC) {
  return ((uint32_t)INT << 15) | ((uint32_t)FRAC << 3) | 0;
}
// R1: prescaler 8/9 (1<<27), phase=1 (1<<15), MOD=2 (2<<3), control 001
uint32_t R1 = (1UL << 27) | (1UL << 15) | (2UL << 3) | 1;
// R2: PD polarity + (1<<6), LDF integer-N (1<<8), CP~2.5mA (7<<9),
//     R counter=1 (1<<14), MUXout=digital lock detect (6<<26), control 010
uint32_t R2 = (1UL << 6) | (1UL << 8) | (7UL << 9) | (1UL << 14) | (6UL << 26) | 2;
// R3: band-select clock high mode (1<<23), CSR enable (1<<18), control 011
uint32_t R3 = (1UL << 23) | (1UL << 18) | 3;
// R4: feedback fundamental (1<<23), RF div /4 (2<<20), RF out on (1<<5),
//     mute till lock (1<<10), band-select clock divider 250, AUX off,
//     RF power level 0 (lowest), control 100
uint32_t R4 = (1UL << 23) | (2UL << 20) | (1UL << 5) | (1UL << 10) | (250UL << 12) | 4;
// R5: LD pin digital (1<<22), LD cycle counter=4 (4<<15), control 101
uint32_t R5 = (1UL << 22) | (4UL << 15) | 5;

void writeReg(uint32_t v) {
  digitalWrite(LE_PIN, LOW);
  for (int i = 31; i >= 0; i--) {
    digitalWrite(SCLK_PIN, LOW);
    digitalWrite(MOSI_PIN, (v >> i) & 1);
    digitalWrite(SCLK_PIN, HIGH);
  }
  digitalWrite(LE_PIN, HIGH);
  digitalWrite(LE_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(LE_PIN, OUTPUT);
  pinMode(MOSI_PIN, OUTPUT);
  pinMode(SCLK_PIN, OUTPUT);
  pinMode(LD_PIN, INPUT);
  pinMode(MUX_PIN, INPUT);
  digitalWrite(LE_PIN, LOW);

  // fPFD = 25 MHz; INT=160 -> VCO = 25*160 = 4000 MHz; RFout = 4000/4 = 1000 MHz
  uint32_t R0 = makeR0(160, 0);

  writeReg(R5);
  writeReg(R4);
  writeReg(R3);
  writeReg(R2);
  writeReg(R1);
  writeReg(R0);
  writeReg(R0);

  delay(200);

  bool locked = false;
  unsigned long t0 = millis();
  while (millis() - t0 < 2000) {
    if (digitalRead(LD_PIN)) { locked = true; break; }
    delay(10);
  }
  Serial.println(locked ? "ADF4350: LOCKED (1 GHz, lowest power out)" : "ADF4350: NOT LOCKED");
}

void loop() {
  Serial.print("LD pin = ");
  Serial.println(digitalRead(LD_PIN) ? "LOCKED" : "unlocked");
  delay(1000);
}
