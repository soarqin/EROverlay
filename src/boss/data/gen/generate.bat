@echo off

echo Replacing words in origin.txt...
python ./replace.py
echo Generating config files for various languages...
python ./generate.py
echo Done.
del /q template.json
