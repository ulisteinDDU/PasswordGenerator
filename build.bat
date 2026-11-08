@echo off
setlocal
cd /d "%~dp0"

rem 工具链位置：默认用项目内 tools\mingw64；迁到别处后 set MINGW64_HOME=新路径 即可
if not defined MINGW64_HOME set "MINGW64_HOME=tools\mingw64"
set "MINGW=%MINGW64_HOME%"
set "GCC=%MINGW%\bin\gcc.exe"
set "WINDRES=%MINGW%\bin\windres.exe"

if not exist "%GCC%" (
  echo [ERROR] Compiler not found: %GCC%
  echo Run tools\fetch_compiler.sh first to get portable MinGW, or fix the path in this script.
  exit /b 1
)

echo [1/2] Compiling resources and sources...
"%WINDRES%" -I . app.rc -O coff -o rsrc.o || ( echo Resource compile failed & exit /b 1 )
"%GCC%" -O2 -municode -mwindows -c src\password_generator.c -o password_generator.o || ( echo Compile failed & exit /b 1 )

echo [2/2] Linking...
"%GCC%" -O2 -municode -mwindows -o PasswordGenerator.exe password_generator.o rsrc.o -lbcrypt -lcomctl32 -lcomdlg32 -lshell32 || ( echo Link failed & exit /b 1 )

del password_generator.o rsrc.o >nul 2>&1

echo.
echo Build OK: PasswordGenerator.exe
endlocal
