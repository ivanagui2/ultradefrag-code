@echo off
;--------------------------------------------------------------------
;                UltraDefrag Boot Time Shell Script
;--------------------------------------------------------------------
; !!! NOTE: THIS FILE MUST BE SAVED IN UNICODE (UTF-16) ENCODING !!!
;--------------------------------------------------------------------

set UD_IN_FILTER=*windows*;*winnt*;*ntuser*;*pagefile.sys;*hiberfil.sys
set UD_EX_FILTER=*temp*;*tmp*;*dllcache*;*ServicePackFiles*

; exclude big fragments which rarely benefit from defragmentation
set UD_FRAGMENT_SIZE_THRESHOLD=20MB

; if your system drive is an SSD uncomment the following
; line to exclude slightly fragmented content as well
; set UD_FRAGMENTS_THRESHOLD=20

; uncomment the following line to increase amount of debugging output
; set UD_DBGPRINT_LEVEL=DETAILED

; uncomment the following line to save debugging output to a log file
; set UD_LOG_FILE_PATH=%UD_INSTALL_DIR%\logs\ud-boot-time.log

udefrag %SystemDrive%

boot-off

exit
