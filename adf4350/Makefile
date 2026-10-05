FQBN    ?= esp8266:esp8266:oak
PORT    ?= /dev/tty.usbserial-2
SKETCH   = .
SRC      = adf4350.ino
STAMP    = .compiled

.PHONY: all compile upload monitor clean

all: compile

$(STAMP): $(SRC)
	perl -pi -e 's/#define SW_MINOR (\d+)/"#define SW_MINOR " . ($$1 + 1)/e' $(SRC)
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)
	touch $(STAMP)

compile: $(STAMP)

upload: $(STAMP)
	arduino-cli upload --fqbn $(FQBN) -p $(PORT) $(SKETCH)

monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=115200

clean:
	rm -f $(STAMP)
