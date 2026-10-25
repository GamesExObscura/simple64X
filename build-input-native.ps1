param([switch]$Check)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$runtime = 'C:/msys64/mingw64'
$out = Join-Path $root 'simple64-input-qt/build-assignment-direct'
$originalPath = $env:PATH
try {
    $env:PATH = "$runtime/bin;$env:SystemRoot/System32;$env:SystemRoot"
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $src = Join-Path $root 'simple64-input-qt'
    & "$runtime/share/qt6/bin/moc.exe" "$src/configdialog.h" -o "$out/moc_configdialog.cpp"
    if ($LASTEXITCODE) { throw 'moc failed' }
    $flags = @('-O2','-DNDEBUG','-std=c++17','-fvisibility=hidden','-DQT_NO_DEBUG','-DQT_WIDGETS_LIB','-DQT_GUI_LIB','-DQT_CORE_LIB','-DQT_NETWORK_LIB','-DUNICODE','-D_UNICODE',"-I$root/mupen64plus-core/src/api", "-I$runtime/include", "-I$runtime/include/qt6")
    foreach ($module in @('QtWidgets','QtGui','QtCore','QtNetwork')) { $flags += "-I$runtime/include/qt6/$module" }
    $objects = @()
    foreach ($name in @('configdialog.cpp','main.cpp','qt2sdl2.cpp')) {
        $obj = "$out/$name.o"
        & "$runtime/bin/g++.exe" @flags -c "$src/$name" -o $obj
        if ($LASTEXITCODE) { throw "Compile failed: $name" }
        $objects += $obj
    }
    & "$runtime/bin/g++.exe" @flags -c "$out/moc_configdialog.cpp" -o "$out/moc.o"
    if ($LASTEXITCODE) { throw 'moc compile failed' }
    & "$runtime/bin/gcc.exe" -O2 "-I$root/mupen64plus-core/src/api" -c "$src/osal/osal_dynamiclib_win32.c" -o "$out/osal.o"
    if ($LASTEXITCODE) { throw 'osal compile failed' }
    & "$runtime/bin/g++.exe" -shared @objects "$out/moc.o" "$out/osal.o" "-L$runtime/lib" -lQt6Widgets -lQt6Network -lQt6Gui -lQt6Core -lSDL2 -o "$out/simple64-input-qt.dll"
    if ($LASTEXITCODE) { throw 'Link failed' }
    Write-Output "Built $out/simple64-input-qt.dll"
    if ($Check) {
        & "$runtime/bin/g++.exe" @flags "$root/input-assignment-check.cpp" "$out/main.cpp.o" "$out/qt2sdl2.cpp.o" "$out/moc.o" "$out/osal.o" "-L$runtime/lib" -lQt6Widgets -lQt6Network -lQt6Gui -lQt6Core -lSDL2 -o "$out/input-assignment-check.exe"
        if ($LASTEXITCODE) { throw 'Test build failed' }
        & "$out/input-assignment-check.exe" -platform offscreen
        if ($LASTEXITCODE) { throw "Assignment tests failed: $LASTEXITCODE" }
    }
} finally {
    $env:PATH = $originalPath
}
