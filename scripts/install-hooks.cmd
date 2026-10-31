@echo off
setlocal
cd /d "%~dp0.."
git config core.hooksPath .githooks
if errorlevel 1 exit /b %errorlevel%
echo Installed .githooks pre-commit hook.
