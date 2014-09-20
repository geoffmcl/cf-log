@setlocal

@set DOINSTALL=0

@set TMPDIR=F:
@REM set TMPRT=%TMPDIR%\FG\18
@set TMPRT=..
@set TMPVER=1
@set TMPPRJ=cf-log
@set TMPSRC=%TMPRT%
@set TMPBGN=%TIME%
@set TMPINS=%TMPRT%\software
@set TMPCM=%TMPSRC%\CMakeLists.txt
@REM set DOPAUSE=pause
@set DOPAUSE=ask
@set TMPSG=X:\install\msvc100\simgear
@set TMP3RD=X:\3rdParty

@if NOT EXIST %TMPSG%\nul (
@echo NOTE: Can NOT locate %TMPSG% directory...
)
@if NOT EXIST %TMP3RD%\nul (
@echo NOTE: Can NOT locate %TMP3RD% directory...
)

@call chkmsvc %TMPPRJ% 

@if EXIST build-cmake.bat (
@call build-cmake
)

@if NOT EXIST %TMPCM% goto NOCM

@set TMPLOG=bldlog-1.txt
@set TMPOPTS=-DCMAKE_INSTALL_PREFIX=%TMPINS%
@REM TODO: tape support - set TMPOPTS=%TMPOPTS% -DADD_RMT_LIB:BOOL=YES
@set TMPOPTS=%TMPOPTS% -DCMAKE_PREFIX_PATH=%TMPSG%;%TMP3RD%

:RPT
@if "%~1x" == "x" goto GOTCMD
@set TMPOPTS=%TMPOPTS% %1
@shift
@goto RPT
:GOTCMD

@echo Build %DATE% %TIME% > %TMPLOG%
@echo Build source %TMPSRC%... all output to build log %TMPLOG%
@echo Build source %TMPSRC%... all output to build log %TMPLOG% >> %TMPLOG%

cmake %TMPSRC% %TMPOPTS% >> %TMPLOG% 2>&1
@if ERRORLEVEL 1 goto ERR1

cmake --build . --config Debug >> %TMPLOG% 2>&1
@if ERRORLEVEL 1 goto ERR2

cmake --build . --config Release >> %TMPLOG% 2>&1
@if ERRORLEVEL 1 goto ERR3
:DONEREL

@REM fa4 "***" %TMPLOG%
@echo.
@call elapsed %TMPBGN%
@echo.
@echo Appears a successful build... see %TMPLOG%
@echo.

@if "%DOINSTALL%x" == "0x" (
@echo.
@echo Skipping install for now...
@echo.
@goto END
)
@%DOPAUSE% Continue with Release install to %TMPINS%? Only 'y' continues...
@if ERRORLEVEL 2 goto NOASK
@if ERRORLEVEL 1 goto DOINST
@echo.
@echo No install at this time...
@echo.
@goto END

:NOASK
@echo.
@echo Warning: Utility ask not found in PATH! SKIPPING INSTALL
@echo.
@goto END

:DOINST

@goto DNDBGINST
cmake --build . --config Debug  --target INSTALL >> %TMPLOG% 2>&1
:DNDBGINST

cmake --build . --config Release  --target INSTALL >> %TMPLOG% 2>&1

@fa4 " -- " %TMPLOG%
@echo.

@call elapsed %TMPBGN%
@echo.
@echo All done build and install ... see %TMPLOG%
@echo.

@goto END

:NOCM
@echo Error: Can NOT locate %TMPCM%
@goto ISERR

:ERR1
@echo cmake configuration or generations ERROR
@goto ISERR

:ERR2
@echo ERROR: Cmake build Debug FAILED!
@goto ISERR

:ERR3
@fa4 "mt.exe : general error c101008d:" %TMPLOG% >nul
@if ERRORLEVEL 1 goto ERR33
:ERR34
@echo ERROR: Cmake build Release FAILED!
@goto ISERR
:ERR33
@echo Try again due to this STUPID STUPID STUPID error
@echo Try again due to this STUPID STUPID STUPID error >>%TMPLOG%
cmake --build . --config Release >> %TMPLOG% 2>&1
@if ERRORLEVEL 1 goto ERR34
@goto DONEREL

:NOSGD
@echo Can NOT locate %TMPSG% directory...
@echo Has X: drive been established
@goto ISERR

:NO3RD
@echo Can NOT locate %TMP3RD% directory...
@echo Has X: drive been established
@goto ISERR


:ISERR
@echo See %TMPLOG% for details...
@endlocal
@exit /b 1

:END
@endlocal
@exit /b 0

@REM eof
