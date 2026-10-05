// ADF4350/ADF4351 frequency synthesizer control over serial
// Oak pin map: P5=LD(in), P6=LE, P7=MOSI(DAT), P9=SCLK(CLK), P10=MUX(in)
// CE must be tied HIGH; PDR pins tied HIGH.
//
// Software revision 0.1

#include <EEPROM.h>

#define SW_MAJOR 0
#define SW_MINOR 10

const int LD_PIN   = P5;
const int LE_PIN   = P6;
const int MOSI_PIN = P7;
const int SCLK_PIN = P9;
const int MUX_PIN  = P10;

const uint32_t EEPROM_MAGIC = 0xADF43501u;

struct Config {
  uint32_t magic;
  uint8_t  major;
  uint8_t  minor;
  uint32_t refHz;        // nominal crystal reference (Hz)
  int64_t  offsetHz;       // +/- offset, Hz, from measured reading
  uint64_t outHz;        // requested output frequency (Hz)
  uint8_t  power;        // 0=lowest .. 3=highest
};

Config cfg;
const uint32_t MODULUS = 2;

enum Device { ADF4350 = 0, ADF4351 = 1 };
Device device = ADF4350;

uint32_t makeR0(uint16_t INT, uint16_t FRAC) {
  return ((uint32_t)INT << 15) | ((uint32_t)FRAC << 3) | 0;
}
// R1: prescaler in bit 27, phase=1 at bit 15, MOD at bits 14:3, control 001
uint32_t makeR1(uint16_t mod, bool usePrescaler89) {
  return (usePrescaler89 ? (1UL << 27) : 0) | (1UL << 15) | ((uint32_t)mod << 3) | 1;
}
// R2: PD polarity negative (default), LDF integer-N, CP~2.5mA, R=1, MUXout=digital LD, control 010
uint32_t R2 = (1UL << 6) | (1UL << 8) | (7UL << 9) | (1UL << 14) | (6UL << 26) | 2;
// R3: band-select clock high mode, CSR enable, control 011
uint32_t R3 = (1UL << 23) | (1UL << 18) | 3;
uint32_t R5 = (1UL << 22) | (4UL << 15) | 5;

uint32_t makeR4(uint8_t divLog2, uint8_t power) {
  return (1UL << 23) | ((uint32_t)divLog2 << 20) | (1UL << 5) | (1UL << 10) |
         (250UL << 12) | (((uint32_t)power & 0x3) << 3) | 4;
}

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

// Returns true if a valid VCO/divider/INT setting was found
bool programADF(uint64_t outHz, uint64_t refHz, uint8_t power) {
  uint64_t ps89Threshold = (device == ADF4350) ? 3000000000ULL : 3600000000ULL;
  for (int d = 0; d <= 6; d++) {          // output divider = 2^d
    uint64_t vco = outHz << d;
    if (vco < 2200000000ULL) continue;
    if (vco > 4400000000ULL) break;
    uint64_t fPFD = refHz;             // R=1, no doubler, no T
    if (fPFD > 32000000ULL) return false;
    uint32_t INT = (uint32_t)(vco / fPFD);
    uint64_t rem = vco - (uint64_t)INT * fPFD;
    uint32_t FRAC = (uint32_t)((rem * MODULUS) / fPFD);
    bool ps89 = (vco > ps89Threshold);
    if (ps89 && INT < 75) continue;
    if (!ps89 && INT < 23) continue;

    uint32_t R0 = makeR0(INT, (uint16_t)FRAC);
    uint32_t R1 = makeR1((uint16_t)MODULUS, ps89);
    uint32_t R4 = makeR4((uint8_t)d, power);

    writeReg(R5);
    writeReg(R4);
    writeReg(R3);
    writeReg(R2);
    writeReg(R1);
    writeReg(R0);
    writeReg(R0);
    return true;
  }
  return false;
}

// AD8307 log detector: Vout = 0.9V + 0.025 V/dB * Pin(dBm)
// Divided by 10k/18k pair: Vnode = Vout * 18/28. Oak A0 range 0..1V.
float readPowerDbm() {
  const int A0_ADC = 1023;
  long acc = 0;
  for (int i = 0; i < 8; i++) acc += analogRead(A0);
  float vNode = (acc / 8.0f) * (1.0f / A0_ADC);
  float vAd8307 = vNode * (28.0f / 18.0f);
  return (vAd8307 - 0.9f) / 0.025f;
}

void defaults() {
  cfg.magic = EEPROM_MAGIC;
  cfg.major = SW_MAJOR;
  cfg.minor = SW_MINOR;
  cfg.refHz = 25000000UL;
  cfg.offsetHz = 0;
  cfg.outHz = 1000000000ULL;
  cfg.power = 0;
}

void loadConfig() {
  EEPROM.get(0, cfg);
  if (cfg.magic != EEPROM_MAGIC) {
    defaults();
    Serial.println(F("No valid EEPROM config; using defaults"));
  } else {
    if (cfg.major != SW_MAJOR || cfg.minor != SW_MINOR) {
      Serial.println(F("EEPROM config from different software revision; using it anyway"));
    }
    if (cfg.power > 3) cfg.power = 3;
  }
}

void saveConfig() {
  cfg.major = SW_MAJOR;
  cfg.minor = SW_MINOR;
  cfg.magic = EEPROM_MAGIC;
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println(F("Configuration saved to EEPROM"));
}

String readLine() {
  String s = Serial.readStringUntil('\n');
  s.trim();
  return s;
}

// Parses "ddd.ffffff" MHz, giving Hz to 1-Hz resolution. Returns false on error.
bool parseMHz(const String &s, uint64_t &hz) {
  int dot = s.indexOf('.');
  String ip = (dot < 0) ? s : s.substring(0, dot);
  String fp = (dot < 0) ? "" : s.substring(dot + 1);
  if (ip.length() == 0 || ip.length() > 10) return false;
  if (fp.length() > 6) return false;
  for (size_t i = 0; i < ip.length(); i++) if (!isDigit(ip[i])) return false;
  for (size_t i = 0; i < fp.length(); i++) if (!isDigit(fp[i])) return false;
  while (fp.length() < 6) fp += '0';
  uint64_t whole = strtoull(ip.c_str(), nullptr, 10);
  uint64_t frac = strtoull(fp.c_str(), nullptr, 10);
  hz = whole * 1000000ULL + frac;   // fp is in millionths of MHz = Hz
  return hz > 0;
}

void printHeader() {
  Serial.println();
  Serial.print(F("Software rev "));
  Serial.print(SW_MAJOR); Serial.print('.'); Serial.println(SW_MINOR);
  Serial.print(F("Device: "));
  Serial.println(device == ADF4350 ? F("ADF4350") : F("ADF4351"));
  Serial.print(F("Ref freq: ")); Serial.print((unsigned long)cfg.refHz);
  Serial.print(F(" Hz, offset: ")); Serial.print((long long)cfg.offsetHz); Serial.println(F(" Hz"));
  Serial.print(F("Requested RF out: ")); Serial.print((unsigned long long)cfg.outHz);
  Serial.print(F(" Hz, est. actual: "));
  Serial.print((long long)((int64_t)cfg.outHz + cfg.offsetHz));
  Serial.println(F(" Hz"));
  Serial.print(F("Power level: ")); Serial.println(cfg.power);
  Serial.print(F("Lock status: "));
  if (digitalRead(LD_PIN)) Serial.print(F("\033[1;32mLOCKED\033[0m"));
  else Serial.print(F("\033[1;31mNOT LOCKED\033[0m"));
  Serial.print(F(", measured RF power: "));
  Serial.print(readPowerDbm()); Serial.println(F(" dBm (AD8307)"));
}

void printMenu() {
  Serial.println(F("--- Menu ---"));
  Serial.println(F("f) Set frequency   (MHz, e.g. 1000.000000)"));
  Serial.println(F("p) Set power level (0=lowest .. 3=highest)"));
  Serial.println(F("r) Set reference   (MHz, e.g. 25.000000)"));
  Serial.println(F("t) Settings (ref correction)"));
  Serial.println(F("d) Select device   (ADF4350 / ADF4351)"));
  Serial.println(F("s) Save settings to EEPROM"));
  Serial.println(F("v) Show status header"));
  Serial.println(F("m) Measure RF power (AD8307 on P11)"));
  Serial.println(F("t) Settings"));
  Serial.print(F("Choice: "));
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(LE_PIN, OUTPUT);
  pinMode(MOSI_PIN, OUTPUT);
  pinMode(SCLK_PIN, OUTPUT);
  pinMode(LD_PIN, INPUT);
  pinMode(MUX_PIN, INPUT);
  digitalWrite(LE_PIN, LOW);

  loadConfig();
  if (programADF(cfg.outHz, cfg.refHz, cfg.power)) {
    delay(200);
  }
  printHeader();
  printMenu();
}

uint64_t promptForMHz(const char *msg) {
  Serial.println(msg);
  while (true) {
    while (!Serial.available()) {}
    uint64_t hz;
    if (parseMHz(readLine(), hz)) return hz;
    Serial.println(F("Bad value, retry (MHz, e.g. 1000.125000):"));
  }
}

void settingsMenu() {
  while (true) {
    Serial.println();
    Serial.print(F("Ref nominal: ")); Serial.print(cfg.refHz);
    Serial.print(F(" Hz, freq offset: ")); Serial.print((long long)cfg.offsetHz);
    Serial.println(F(" Hz"));
    Serial.println(F("Settings:"));
    Serial.println(F("c) Apply frequency offset from measured frequency"));
    Serial.println(F("0) Reset frequency offset to 0"));
    Serial.println(F("b) Back to main menu"));
    Serial.print(F("Choice: "));
    while (!Serial.available()) {}
    String line = readLine();
    char c = line.length() ? line[0] : '?';
    if (c == 'c' || c == 'C') {
      uint64_t measured = promptForMHz("Measured output frequency in MHz:");
      if (measured > 0) {
        cfg.offsetHz = (int64_t)measured - (int64_t)cfg.outHz;
        Serial.print(F("New frequency offset: "));
        Serial.print((long long)cfg.offsetHz);
        Serial.println(F(" Hz"));
        if (programADF(cfg.outHz, cfg.refHz, cfg.power)) {
          Serial.println(F("Programmed"));
          saveConfig();
        } else Serial.println(F("Parameter out of range"));
      }
    } else if (c == '0') {
      cfg.offsetHz = 0;
      Serial.println(F("Frequency offset reset to 0"));
      if (programADF(cfg.outHz, cfg.refHz, cfg.power)) {
        Serial.println(F("Programmed"));
        saveConfig();
      } else Serial.println(F("Parameter out of range"));
    } else if (c == 'b' || c == 'B') {
      return;
    } else {
      Serial.println(F("Unknown choice"));
    }
  }
}

void loop() {
  if (Serial.available()) {
    String line = readLine();
    char c = line.length() ? line[0] : '?';
    switch (c) {
      case 'f': case 'F': {
        uint64_t hz = promptForMHz("Frequency in MHz:");
        uint64_t old = cfg.outHz;
        cfg.outHz = hz;
        if (!programADF(cfg.outHz, cfg.refHz, cfg.power)) {
          cfg.outHz = old;
          Serial.println(F("Frequency out of range; not applied"));
          break;
        }
        Serial.println(F("Programmed"));
        saveConfig();
        break;
      }
      case 'p': case 'P': {
        Serial.println(F("Power level (0-3):"));
        while (!Serial.available()) {}
        int p = readLine().toInt();
        if (p < 0 || p > 3) Serial.println(F("Must be 0..3"));
        else {
          cfg.power = p;
          if (programADF(cfg.outHz, cfg.refHz, cfg.power)) {
            Serial.println(F("Programmed"));
            saveConfig();
          } else Serial.println(F("Frequency out of range"));
        }
        break;
      }
      case 'r': case 'R': {
        uint64_t hz = promptForMHz("Reference frequency in MHz:");
        if (hz < 100000 || hz > 32000000ULL) {
          Serial.println(F("Reference must be 0.1..32 MHz; not applied"));
          break;
        }
        uint32_t oldRef = cfg.refHz;
        cfg.refHz = (uint32_t)hz;
        cfg.offsetHz = 0;
        if (!programADF(cfg.outHz, cfg.refHz, cfg.power)) {
          cfg.refHz = oldRef;
          Serial.println(F("Parameter out of range; not applied"));
          break;
        }
        Serial.println(F("Programmed (freq offset reset)"));
        saveConfig();
        break;
      }
      case 'c': case 'C':
        Serial.println(F("Use 't' (Settings) for reference correction"));
        break;
      case 't': case 'T':
        settingsMenu();
        break;
      case 'd': case 'D': {
        Serial.println(F("Enter 0 for ADF4350, 1 for ADF4351:"));
        while (!Serial.available()) {}
        String ds = readLine();
        if (ds == "0") { device = ADF4350; Serial.println(F("ADF4350 selected")); }
        else if (ds == "1") { device = ADF4351; Serial.println(F("ADF4351 selected")); }
        else Serial.println(F("Must be 0 or 1"));
        if (programADF(cfg.outHz, cfg.refHz, cfg.power))
          Serial.println(F("Re-programmed current settings"));
        break;
      }
      case 's': case 'S':
        saveConfig();
        break;
      case 'm': case 'M': {
        Serial.print(F("Measured RF power: "));
        Serial.print(readPowerDbm()); Serial.println(F(" dBm"));
        break;
      }
      case 'v': case 'V':
        break;
      default:
        if (line.length() > 0) Serial.println(F("Unknown choice"));
    }
    printHeader();
    printMenu();
  }
}
