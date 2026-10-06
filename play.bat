@echo off
rem Open the OpenFlightSim launcher from this checkout. First use prepares everything.
setlocal
where py >nul 2>nul && (set "OFS_PYTHON=py -3") || (set "OFS_PYTHON=python")
%OFS_PYTHON% "%~dp0scripts\play.py" %*
if errorlevel 1 (
  echo.
  echo OpenFlightSim could not start. See the messages above.
  echo If Python is missing, install Python 3 from https://www.python.org/downloads/
  pause
)
