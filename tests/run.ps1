$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$dir = Join-Path $tempRoot ("LeakTrace7_" + [guid]::NewGuid().ToString('N'))
New-Item -Path $dir -ItemType Directory | Out-Null
try {
    foreach ($name in @('core','portable','adapters','48h','time_future')) {
        & gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic "test_$name.c" -o (Join-Path $dir "$name.exe")
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $name" }
        & (Join-Path $dir "$name.exe")
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $name" }
    }
    $common = @('-DLT_MAX_LIVE=16u','-DLT_BUCKETS=8u','-DLT_FAST_SLOTS=4u','-DLT_SLOW_SLOTS=4u')
    & gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic @common test_stdlib.c ../LeakTrace_Stdlib.c -o (Join-Path $dir 'stdlib.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Compile failed: stdlib' }
    & (Join-Path $dir 'stdlib.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Test failed: stdlib' }
    $common[0] = '-DLT_MAX_LIVE=32u'
    $common[1] = '-DLT_BUCKETS=16u'
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic @common test_cpp.cpp ../LeakTrace_Cpp.cpp -o (Join-Path $dir 'cpp.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Compile failed: cpp' }
    & (Join-Path $dir 'cpp.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Test failed: cpp' }
    Write-Host 'PASS: all standard tests'
}
finally {
    $resolvedDir = [IO.Path]::GetFullPath($dir)
    if (-not $resolvedDir.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a test directory outside the temporary workspace.'
    }
    Remove-Item -LiteralPath $resolvedDir -Recurse -Force
    Pop-Location
}
