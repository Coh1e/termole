param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Evidence)

$ErrorActionPreference = 'Stop'
$root = [IO.Directory]::CreateDirectory($Evidence).FullName
$entry = (Get-Item -LiteralPath "$Package/share/termole/examples/select-file.ps1").FullName
Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class PreflightJob {
    [StructLayout(LayoutKind.Sequential)]
    struct Accounting {
        public long user, kernel, periodUser, periodKernel;
        public uint faults, totalProcesses, active, terminated;
    }
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern SafeFileHandle CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32", SetLastError = true)]
    public static extern bool AssignProcessToJobObject(SafeFileHandle job, IntPtr process);
    [DllImport("kernel32", SetLastError = true)]
    static extern bool QueryInformationJobObject(SafeFileHandle job, int kind, out Accounting info,
                                                uint size, IntPtr returned);
    [DllImport("kernel32", SetLastError = true)]
    public static extern bool TerminateJobObject(SafeFileHandle job, uint exitCode);
    public static uint ProcessCount(SafeFileHandle job) {
        Accounting info;
        if (!QueryInformationJobObject(job, 1, out info, (uint)Marshal.SizeOf(typeof(Accounting)), IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return info.totalProcesses;
    }
}
'@

# The picker cannot start before its PowerShell caller joins the job. The job
# retains its cumulative count even if a launcher opens and exits immediately.
$childCommand = @'
$gate = [Threading.EventWaitHandle]::OpenExisting($env:TERMOLE_TEST_GATE)
$ready = [Threading.EventWaitHandle]::OpenExisting($env:TERMOLE_TEST_GATE + '-ready')
[void]$ready.Set()
$ready.Dispose()
try { if (!$gate.WaitOne(15000)) { exit 90 } } finally { $gate.Dispose() }
& $env:TERMOLE_TEST_ENTRY -Directory ([string]$env:TERMOLE_TEST_DIRECTORY)
exit $LASTEXITCODE
'@
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($childCommand))
$empty = [IO.Directory]::CreateDirectory("$root/empty").FullName
foreach ($case in @(
    @{ Name = 'empty-argument'; Directory = ''; Code = 2; Diagnostic = 'Invalid directory' },
    @{ Name = 'empty-directory'; Directory = $empty; Code = 1; Diagnostic = 'No files in directory' },
    @{ Name = 'missing-directory'; Directory = "$root/missing"; Code = 2; Diagnostic = 'Invalid directory' },
    @{ Name = 'file-as-directory'; Directory = "$Package/README.md"; Code = 2; Diagnostic = 'Invalid directory' },
    @{ Name = 'missing-fzf'; Directory = $Package; Code = 2; Diagnostic = 'fzf.exe was not found on PATH' }
)) {
    $gateName = 'TermolePreflight-' + [Guid]::NewGuid().ToString('N')
    $gate = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $gateName)
    $ready = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, "$gateName-ready")
    $job = [PreflightJob]::CreateJobObject([IntPtr]::Zero, $null)
    $p = $null
    try {
        if ($job.IsInvalid) { throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error()) }
        $start = [Diagnostics.ProcessStartInfo]::new("$PSHOME/powershell.exe",
            "-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $encoded")
        $start.WorkingDirectory = $Package
        $start.CreateNoWindow = $true
        $start.UseShellExecute = $false
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $start.EnvironmentVariables['PATH'] = ''
        $start.EnvironmentVariables['TERMOLE_TEST_GATE'] = $gateName
        $start.EnvironmentVariables['TERMOLE_TEST_ENTRY'] = $entry
        $start.EnvironmentVariables['TERMOLE_TEST_DIRECTORY'] = $case.Directory
        $p = [Diagnostics.Process]::Start($start)
        if (!$ready.WaitOne(15000)) { throw "PowerShell did not reach preflight for $($case.Name)" }
        if (![PreflightJob]::AssignProcessToJobObject($job, $p.Handle)) {
            throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
        }
        [void]$gate.Set()
        $stdout = [IO.File]::Create("$root/$($case.Name).stdout")
        try {
            $copy = $p.StandardOutput.BaseStream.CopyToAsync($stdout)
            $stderr = $p.StandardError.ReadToEndAsync()
            if (!$p.WaitForExit(15000)) { throw "Preflight timed out for $($case.Name)" }
            $count = [PreflightJob]::ProcessCount($job)
            if ($count -ne 1) { throw "$($case.Name) started $($count - 1) unexpected processes before returning" }
            $null = $copy.GetAwaiter().GetResult()
            $diagnostic = $stderr.GetAwaiter().GetResult()
            [IO.File]::WriteAllText("$root/$($case.Name).stderr", $diagnostic)
            if ($p.ExitCode -ne $case.Code -or $stdout.Length -ne 0 -or !$diagnostic.Contains($case.Diagnostic)) {
                throw "Unexpected $($case.Name) result: exit=$($p.ExitCode), stdout=$($stdout.Length) bytes, stderr=$diagnostic"
            }
            Write-Output "$($case.Name) exit=$($p.ExitCode), stdout empty, no child processes"
        } finally { $stdout.Dispose() }
    } finally {
        if (!$job.IsInvalid) { [void][PreflightJob]::TerminateJobObject($job, 1) }
        if ($p) {
            if (!$p.HasExited) { $p.Kill() }
            $p.Dispose()
        }
        $job.Dispose()
        $gate.Dispose()
        $ready.Dispose()
    }
}
