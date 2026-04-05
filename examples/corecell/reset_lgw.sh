#!/bin/sh

# This script uses gpioset (libgpiod) to reset SX1302 CoreCell.
# Board profile: SX1302 present, SX1261 not present.
# For raspberry pi, remember to use the GPIO number instead of physical pin number. 
# For example, for raspberry pi 4, GPIO23 is physical pin 16

GPIO_CHIP="gpiochip0"
SX1302_RESET_PIN=23
SX1302_POWER_EN_PIN=18

WAIT_GPIO() {
    sleep 0.1
}

require_gpioset() {
    if ! command -v gpioset >/dev/null 2>&1; then
        echo "ERROR: gpioset not found. Please install libgpiod tools."
        exit 1
    fi
}

reset_lgw() {
    echo "Accessing $GPIO_CHIP..."
    echo "CoreCell power enable through GPIO$SX1302_POWER_EN_PIN..."
    echo "CoreCell reset through GPIO$SX1302_RESET_PIN..."

    gpioset "$GPIO_CHIP" "$SX1302_POWER_EN_PIN"=1; WAIT_GPIO
    gpioset "$GPIO_CHIP" "$SX1302_RESET_PIN"=1; WAIT_GPIO
    gpioset "$GPIO_CHIP" "$SX1302_RESET_PIN"=0; WAIT_GPIO
}

power_down() {
    echo "Powering down concentrator..."
    gpioset "$GPIO_CHIP" "$SX1302_POWER_EN_PIN"=0
    gpioset "$GPIO_CHIP" "$SX1302_RESET_PIN"=1
}

case "$1" in
    start)
        require_gpioset
        reset_lgw
        echo "Concentrator reset sequence complete."
        ;;
    stop)
        require_gpioset
        power_down
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
        ;;
esac

exit 0