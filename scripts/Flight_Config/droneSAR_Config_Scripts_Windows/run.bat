@echo off
setlocal
SET RP_HOST=root@10.42.0.185
SET WORKING_DIR=/opt/redpitaya/milosar
SET TRIGGER_DIR=/opt/redpitaya/milosar_trigger
call :GetUnixTime UNIX_TIME
ECHO.

ECHO "Configuring Purdue MiloSAR"
ECHO "10.42.0.185"
ECHO.

ECHO "---> Setting the date and time"
ssh %RP_HOST% "date -s @'%UNIX_TIME%'"
ECHO.

ECHO "---> Enabling read/write mode"
ssh -t %RP_HOST% "bash -l 'rw'"
ECHO.

ECHO "---> Copying new setup.ini configuration file"
scp setup.ini %RP_HOST%:%WORKING_DIR%
ECHO.

ECHO "---> Enabling Radar" 
ssh -t %RP_HOST% "cd %TRIGGER_DIR%; ./milosar_trigger"
ECHO ""
goto :EOF

:GetUnixTime
setlocal enableextensions
for /f %%x in ('wmic path win32_utctime get /format:list ^| findstr "="') do (
    set %%x)
set /a z=(14-100%Month%%%100)/12, y=10000%Year%%%10000-z
set /a ut=y*365+y/4-y/100+y/400+(153*(100%Month%%%100+12*z-3)+2)/5+Day-719469
set /a ut=ut*86400+100%Hour%%%100*3600+100%Minute%%%100*60+100%Second%%%100
endlocal & set "%1=%ut%" & goto :EOF