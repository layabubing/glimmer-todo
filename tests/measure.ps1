param([int]$DurationSeconds = 60, [int]$IntervalMilliseconds = 500)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$target = Get-Process -Name GlimmerTodo
if (@($target).Count -ne 1) { throw '需要且仅需要一个正在运行的 GlimmerTodo 进程。' }
$initialCpu = $target.CPU
$watch = [Diagnostics.Stopwatch]::StartNew()
$samples = [Collections.Generic.List[object]]::new()
while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
    $target.Refresh()
    if ($target.HasExited) { throw '待办进程提前退出。' }
    $samples.Add([pscustomobject]@{
        seconds = [math]::Round($watch.Elapsed.TotalSeconds, 2)
        workingSetBytes = $target.WorkingSet64
        privateBytes = $target.PrivateMemorySize64
        handles = $target.HandleCount
    })
    Start-Sleep -Milliseconds $IntervalMilliseconds
}
$target.Refresh()
$report = [pscustomobject]@{
    processId = $target.Id
    durationSeconds = [math]::Round($watch.Elapsed.TotalSeconds, 2)
    sampleCount = $samples.Count
    minimumWorkingSetMB = [math]::Round(($samples.workingSetBytes | Measure-Object -Minimum).Minimum / 1000000, 2)
    maximumWorkingSetMB = [math]::Round(($samples.workingSetBytes | Measure-Object -Maximum).Maximum / 1000000, 2)
    peakWorkingSetMB = [math]::Round($target.PeakWorkingSet64 / 1000000, 2)
    maximumPrivateMB = [math]::Round(($samples.privateBytes | Measure-Object -Maximum).Maximum / 1000000, 2)
    cpuSeconds = [math]::Round($target.CPU - $initialCpu, 4)
    samples = $samples
}
New-Item -ItemType Directory -Force '.work' | Out-Null
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath '.work\memory.json' -Encoding utf8
$report | Select-Object -ExcludeProperty samples | Format-List
if ($report.maximumWorkingSetMB -gt 50) { throw '实测工作集超过 50 MB。' }
