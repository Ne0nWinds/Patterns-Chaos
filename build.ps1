param(
    [switch]$run,
	[switch]$debug,
	[switch]$optimize,
	[switch]$shaders
)

# Ensure Vulkan SDK is set
$VULKAN_SDK = $env:VULKAN_SDK
if (-not $VULKAN_SDK) {
    Write-Host "Error: Vulkan SDK not found. Please install it and set VULKAN_SDK environment variable." -ForegroundColor Red
    exit 1
}

if ($shaders) {
	Write-Host "Compiling shaders:" -ForegroundColor Cyan

	$commands = @(
		"glslc -mfmt=c -fshader-stage=compute .\clear.compute.glsl -o clear.compute.h",
		"glslc -mfmt=c -fshader-stage=compute .\reset.compute.glsl -o reset.compute.h",
		"glslc -mfmt=c -fshader-stage=compute .\fade.compute.glsl -o fade.compute.h",
		"glslc -mfmt=c -fshader-stage=compute .\simulate.compute.glsl -o simulate.compute.h"
	)

	foreach ($CMD in $commands) {
		Write-Host $CMD -ForegroundColor Yellow
		if ($debug) {
			Invoke-Expression "$CMD -g"
		} else {
			Invoke-Expression $CMD
		}
		if ($LASTEXITCODE -eq 0) {
			Write-Host "Shader compilation successful!" -ForegroundColor Green
		} else {
			Write-Host "Shader compilation successful failed" -ForegroundColor Red
			exit 1
		}
		Write-Host ""
	}
}

# Compiler and output settings
$COMPILER = "clang++"
$SRC = "main.cpp"
$OUT = "PPS.exe"
$FLAGS = "-std=c++20"

if ($optimize) {
	$FLAGS += " -O2"
} else {
	$FLAGS += " -DDEBUG_MODE"
}

if ($debug) {
	$FLAGS += " -g"
	$env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"
} else {
	$env:VK_INSTANCE_LAYERS=""
}

# Include and Library paths
$INCLUDE = "-I`"$VULKAN_SDK\Include`"" 
$LIBPATH = "-L`"$VULKAN_SDK\Lib`" -L`".\GLFW`""
$LIBS = "-lvulkan-1 -lglfw3 -luser32 -lgdi32 -lshell32"

# Compile command
$CMD = "$COMPILER $SRC -o $OUT $INCLUDE $LIBPATH $LIBS $FLAGS"

# Execute compilation
Write-Host "Compiling with command:" -ForegroundColor Cyan
Write-Host $CMD -ForegroundColor Yellow
Invoke-Expression $CMD

# Check if compilation was successful
if ($LASTEXITCODE -eq 0) {
	if ($run) {
		Invoke-Expression ".\$OUT"
	} else {
		Write-Host "Compilation successful! Run .\$OUT to execute the program." -ForegroundColor Green
	}
} else {
    Write-Host "Compilation failed." -ForegroundColor Red
}
