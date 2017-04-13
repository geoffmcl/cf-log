@setlocal
@set TMPEXE=Release\raw-log.exe
@if NOT EXIST %TMPEXE% goto NOEXE

%TMPEXE% %*

@goto END
:NOEXE
@echo Error: Can NOT locate %TMPEXE%! *** FIX ME ***
:END

