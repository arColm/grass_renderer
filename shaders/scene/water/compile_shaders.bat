@echo off
for %%i in (*.vert *.frag *.comp) do (
	%VULKAN_SDK%/Bin/glslangValidator.exe -V "%%~i" -o "%%~i.spv"
)
echo success
pause