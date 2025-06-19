#!/bin/bash
RP_HOST=root@10.42.0.185
WORKING_DIR=/opt/redpitaya/milosar
TRIGGER_DIR=/opt/redpitaya/milosar_trigger

clear

echo "Configuring Purdue MiloSAR"
echo "10.42.0.185"
echo ""

echo "---> Setting the date and time"
ssh $RP_HOST "date -s @'$(date +%s)'"
echo ""

echo "---> Enabling read/write mode"
ssh -t $RP_HOST "bash -l 'rw'"
echo ""

echo "---> Copying new setup.ini configuration file"
scp setup.ini $RP_HOST:$WORKING_DIR
echo ""

echo "---> Enabling Radar" 
ssh -t $RP_HOST "cd $TRIGGER_DIR; ./milosar_trigger"
echo ""
