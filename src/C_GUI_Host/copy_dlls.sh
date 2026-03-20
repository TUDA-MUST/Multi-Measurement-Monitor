#!/bin/bash

# Source and destination folders
SRC="/c/msys64/mingw64/bin"
DST="/d/Hiwi_MUST/Suppelt_ADC/git-multi-ferro-daq/Multi-Ferro-DAQ/Migration_from_MATLAB/C_GUI_Host/WinApp/bin"

# Make sure destination exists
mkdir -p "$DST"

# Copy all DLL files
cp -u "$SRC"/*.dll "$DST"

echo "All DLLs copied from $SRC to $DST"
