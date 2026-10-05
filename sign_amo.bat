@echo off
REM ============================================================
REM  XVidGrab - AMO signer (makes the extension PERMANENTLY
REM  installable on any release Firefox)
REM
REM  One-time setup (free, ~2 minutes):
REM   1. Create a free account at https://addons.mozilla.org/developers/
REM   2. Generate API keys at:
REM      https://addons.mozilla.org/developers/addon/api/key/
REM   3. In this window run:
REM        set AMO_KEY=your-jwt-issuer
REM        set AMO_SECRET=your-jwt-secret
REM      ...then run this .bat again.
REM
REM  Requires Node.js (for npx web-ext). The signed .xpi lands in
REM  the "dist" folder next to this file - install it via
REM  about:addons -> gear icon -> "Install Add-on From File...".
REM ============================================================
if "%AMO_KEY%"=="" echo [X] Set AMO_KEY and AMO_SECRET first (see instructions inside this file). & exit /b 1
if "%AMO_SECRET%"=="" echo [X] Set AMO_KEY and AMO_SECRET first (see instructions inside this file). & exit /b 1
where npx >nul 2>nul || (echo [X] Node.js not found - install from https://nodejs.org & exit /b 1)
npx --yes web-ext sign --api-key "%AMO_KEY%" --api-secret "%AMO_SECRET%" --channel unlisted --source-dir "%~dp0." --artifacts-dir "%~dp0dist"
if errorlevel 1 echo [X] Signing failed - check the error above. & exit /b 1
echo.
echo [OK] Signed XVidGrab .xpi is in: %~dp0dist
echo      Install: about:addons -^> gear icon -^> Install Add-on From File...
pause
