@echo off
rem jtty-sidecar - release a new version in one command. See deploy.ps1 for what it does.
rem   deploy            next patch version
rem   deploy minor      next minor version
rem   deploy major      next major version
rem   deploy 1.4.2      exactly that version
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" %*
exit /b %errorlevel%
