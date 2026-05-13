HHKB Bridge Schematic
======================

Sheet 1: Power

USB-C Female (charging port, J2):
  VBUS -> CHG_5V_IN net
  CC1 -> 5.1kohm -> GND
  CC2 -> 5.1kohm -> GND
  GND -> GND

BQ24075 (U2) -- Battery charger with power path:
  IN -> CHG_5V_IN (USB charger input)
  BAT -> LiPo BAT+ (battery positive)
  OUT -> TPS61023 VIN (system output to boost converter)
  PG -> GPIO6 (charger status to ESP32, active low)
  EN1, EN2 -> GND (500mA charge current)
  TS -> 10kohm -> GND (thermistor disabled)
  ILIM -> via resistor -> GND

TPS61023 (U3) -- Boost converter 3.7V -> 5V:
  VIN -> BQ24075 OUT
  VOUT -> 5V_BOOST net
  EN -> GPIO4 (boost enable from ESP32)

Power Mux (D1, D2 -- BAT54C dual Schottky diode):
  5V_DIRECT (CHG_5V_IN) -> diode A anode
  5V_BOOST (TPS61023 VOUT) -> diode B anode
  diode A cathode + diode B cathode -> 5V_RAIL net

AMS1117-3.3 (U4) -- 3.3V LDO:
  VIN -> 5V_RAIL
  VOUT -> 3.3V rail
  C_IN: 10uF to GND
  C_OUT: 10uF to GND

5V_RAIL -> USB-C(M) VBUS (power to HHKB)

Sheet 2: ESP32-S3 + USB Host

ESP32-S3-MINI-1 (U1):
  VDD -> 3.3V rail
  EN -> 3.3V (pull-up)
  GPIO19 (USB_OTG_DN) -> USB-C(M) D-
  GPIO20 (USB_OTG_DP) -> USB-C(M) D+
  GPIO18 (USB_OTG_ID) -> GND (host mode)
  GPIO4 -> TPS61023 EN (boost enable)
  GPIO5 -> midpoint of 100k/100k divider from LiPo BAT+ (ADC)
  GPIO6 -> BQ24075 PG (charger status)

USB-C Male (to HHKB, J1):
  VBUS -> 5V_RAIL (power to HHKB)
  D- -> GPIO19
  D+ -> GPIO20
  CC1, CC2 -> NC (host side)
  GND -> GND

Sheet 3: Peripheral Signals

Battery voltage divider:
  LiPo BAT+ -> 100kohm (R1) -> GPIO5 (ADC) -> 100kohm (R2) -> GND
  (At 4.2V full charge, GPIO5 sees 2.1V -- safe for 3.3V ADC)

Boost enable:
  GPIO4 -> TPS61023 EN
  ESP32 drives high on boot. When charger connected, 5V_RAIL
  is powered via direct path regardless of boost state.

Charger status:
  GPIO6 -> BQ24075 PG (low = power good, high = fault/charging)
