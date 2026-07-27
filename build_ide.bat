@echo off
cd /d "%~dp0"
title E Temple IDE v4.17 Builder

echo ================================================
echo E TEMPLE IDE v4.17
echo GCC / C++11 kompatibilis
echo ================================================
echo.

windres resources.rc -O coff -o resources.o
if errorlevel 1 (
  echo HIBA: a windres nem indult el. A MinGW bin mappaja legyen PATH-ban.
  pause
  exit /b 1
)

g++ -std=c++11 main.cpp app.cpp editor.cpp compiler.cpp process.cpp resources.o -o e_ide.exe -mwindows -lgdi32 -luser32 -lkernel32 -lcomdlg32
if errorlevel 1 (
 echo BUILD FAILED
 pause
 exit /b 1
)
echo BUILD COMPLETE: e_ide.exe
pause
