#! /bin/bash

DEV=0000:06:00.3
DRV=$(basename "$(readlink /sys/bus/pci/devices/$DEV/driver)")
echo $DEV | sudo tee /sys/bus/pci/drivers/$DRV/unbind
sleep 1
echo $DEV | sudo tee /sys/bus/pci/drivers/$DRV/bind
