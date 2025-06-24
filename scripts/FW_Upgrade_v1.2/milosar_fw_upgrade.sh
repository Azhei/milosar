#!/bin/bash
RP_HOST=root@rp-f0b92e.local
WORKING_DIR=/opt/redpitaya/milosar
TRIGGER_DIR=/opt/redpitaya/milosar_trigger
FIRMWARE_DIR="/lib/firmware/milosar"
# Specify backup dir according to date and time
TODAY=$(date +_%Y%m%d_%H%M%S)
BACKUP_DIR=/opt/redpitaya/milosar_BAK$TODAY
TRIGGER_BACKUP_DIR=/opt/redpitaya/milosar_trigger_BAK$TODAY
VERSION=1.2.0

clear

echo "Firmware upgrade for MiloSAR"
echo "IP: $RP_HOST"
echo ""

echo "---> Setting the date and time"
ssh $RP_HOST "date -s @'$(date +%s)'"
echo ""

echo "---> Enabling read/write mode"
ssh -t $RP_HOST "bash -l 'rw'"
echo ""

echo "---> Backup current firmware"
ssh -t $RP_HOST "if [ -d $WORKING_DIR ]; then mkdir $BACKUP_DIR; cp -r $WORKING_DIR $BACKUP_DIR; rm -r $WORKING_DIR; fi; mkdir $WORKING_DIR"
ssh -t $RP_HOST "if [ -d $TRIGGER_DIR ]; then mkdir $TRIGGER_BACKUP_DIR; cp -r $TRIGGER_DIR $TRIGGER_BACKUP_DIR; rm -r $TRIGGER_DIR; fi; mkdir $TRIGGER_DIR"

echo ""

echo "---> Upgrading firmware"
ssh $RP_HOST "mkdir -p $FIRMWARE_DIR"
scp system_wrapper.bit.bin $RP_HOST:$FIRMWARE_DIR
scp system_wrapper.bit.bin $RP_HOST:$WORKING_DIR
scp setup.ini $RP_HOST:$WORKING_DIR
scp milosar $RP_HOST:$WORKING_DIR
scp -r ramps $RP_HOST:$WORKING_DIR
scp -r template $RP_HOST:$WORKING_DIR
echo ""
# trigger app
echo "---> Upgrading trigger app"
scp milosar_trigger $RP_HOST:$TRIGGER_DIR
scp system_wrapper.bit.bin $RP_HOST:$TRIGGER_DIR
echo ""

echo "---> Upgraded to MiloSAR v$VERSION" 
