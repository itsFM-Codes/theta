@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ was not found in PATH.
    exit /b 1
)

if not exist "build" mkdir "build"
if errorlevel 1 (
    echo [ERROR] Could not create the build directory.
    exit /b 1
)

set "CXX=g++"
set "CXXFLAGS=-std=c++17 -Wall -Wextra -Wpedantic -I."

echo [BUILD] position_test
%CXX% %CXXFLAGS% ^
    tests\position_test.cpp ^
    src\chess\board.cpp ^
    src\chess\position.cpp ^
    -o build\position_test.exe
if errorlevel 1 exit /b 1

echo [TEST]  position_test
build\position_test.exe
if errorlevel 1 exit /b 1

echo [BUILD] move_test
%CXX% %CXXFLAGS% ^
    tests\move_test.cpp ^
    src\chess\board.cpp ^
    src\chess\position.cpp ^
    src\chess\move.cpp ^
    -o build\move_test.exe
if errorlevel 1 exit /b 1

echo [TEST]  move_test
build\move_test.exe
if errorlevel 1 exit /b 1

echo [BUILD] movegen_test
%CXX% %CXXFLAGS% ^
    tests\movegen_test.cpp ^
    src\chess\board.cpp ^
    src\chess\position.cpp ^
    src\chess\move.cpp ^
    src\chess\movegen.cpp ^
    -o build\movegen_test.exe
if errorlevel 1 exit /b 1

echo [TEST]  movegen_test
build\movegen_test.exe
if errorlevel 1 exit /b 1

echo [BUILD] legal_movegen_test
%CXX% %CXXFLAGS% ^
    tests\legal_movegen_test.cpp ^
    src\chess\board.cpp ^
    src\chess\position.cpp ^
    src\chess\fen.cpp ^
    src\chess\move.cpp ^
    src\chess\movegen.cpp ^
    -o build\legal_movegen_test.exe
if errorlevel 1 exit /b 1

echo [TEST]  legal_movegen_test
build\legal_movegen_test.exe
if errorlevel 1 exit /b 1

echo [BUILD] fen_test
%CXX% %CXXFLAGS% ^
    tests\fen_test.cpp ^
    src\chess\board.cpp ^
    src\chess\position.cpp ^
    src\chess\fen.cpp ^
    src\chess\move.cpp ^
    src\chess\movegen.cpp ^
    -o build\fen_test.exe
if errorlevel 1 exit /b 1

echo [TEST]  fen_test
build\fen_test.exe
if errorlevel 1 exit /b 1

echo [BUILD] evaluation_test
%CXX% %CXXFLAGS% ^
    tests\evaluation_test.cpp ^
    src\chess\board.cpp ^
    src\chess\position.cpp ^
    src\eval\evaluation.cpp ^
    -o build\evaluation_test.exe
if errorlevel 1 exit /b 1

echo [TEST]  evaluation_test
build\evaluation_test.exe
if errorlevel 1 exit /b 1

echo [BUILD] transposition_table_test
%CXX% %CXXFLAGS% ^
    tests\transposition_table_test.cpp ^
    src\chess\board.cpp ^
    src\chess\position.cpp ^
    src\chess\move.cpp ^
    src\chess\zobrist.cpp ^
    src\engine\transposition_table.cpp ^
    -o build\transposition_table_test.exe
if errorlevel 1 exit /b 1

echo [TEST]  transposition_table_test
build\transposition_table_test.exe
if errorlevel 1 exit /b 1

echo [BUILD] search_test
%CXX% %CXXFLAGS% ^
    tests\search_test.cpp ^
    src\chess\board.cpp ^
    src\chess\position.cpp ^
    src\chess\move.cpp ^
    src\chess\movegen.cpp ^
    src\chess\zobrist.cpp ^
    src\engine\search.cpp ^
    src\engine\search_context.cpp ^
    src\engine\move_ordering.cpp ^
    src\engine\quiescence.cpp ^
    src\engine\transposition_table.cpp ^
    src\eval\evaluation.cpp ^
    -o build\search_test.exe
if errorlevel 1 exit /b 1

echo [TEST]  search_test
build\search_test.exe
if errorlevel 1 exit /b 1

set "ENGINE_SOURCES="
for /r "src" %%F in (*.cpp) do (
    set "ENGINE_SOURCES=!ENGINE_SOURCES! "%%F""
)

echo [BUILD] theta
%CXX% %CXXFLAGS% main.cpp !ENGINE_SOURCES! -o build\theta.exe
if errorlevel 1 exit /b 1

echo.
echo All tests passed.
echo Engine: build\theta.exe
exit /b 0
