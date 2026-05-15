# Notepatra palette preview - synthetic; no real data
# Exercises: param block, function, $variable, cmdlets (Get-/Set-/Invoke-),
# try/catch, hashtable, pipeline, control flow.

[CmdletBinding()]
param(
    [string]$Name = 'Alice',
    [int]$Retries = 0x10,
    [switch]$Verbose
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$PI = 3.14159
$Users = @(
    [pscustomobject]@{ Id = 1; Name = 'Alice'; Email = 'alice@example.com' }
    [pscustomobject]@{ Id = 2; Name = 'Bob';   Email = 'bob@example.org'  }
)

$StatusCounts = @{
    Pending  = 0
    Active   = 2
    Archived = 1
}

function Get-Greeting {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Name,
        [string]$Email = 'unknown@example.com'
    )
    return "hello $Name <$Email>"
}

function Classify-Value {
    param($Value)
    switch ($Value) {
        $null         { return 'null' }
        { $_ -is [int] -and $_ -lt 0 } { return "negative:$Value" }
        { $_ -is [int] }              { return "int:$Value" }
        { $_ -is [string] }           { return "str:$Value" }
        default                       { return 'unknown' }
    }
}

try {
    Write-Host "pi=$PI retries=$Retries"

    $Users |
        Where-Object { $_.Email -like '*@example.*' } |
        ForEach-Object { Get-Greeting -Name $_.Name -Email $_.Email } |
        Write-Output

    foreach ($k in $StatusCounts.Keys) {
        Write-Host ("{0} => {1}" -f $k, $StatusCounts[$k])
    }

    @(-3, 42, 'ok', $null) | ForEach-Object {
        Write-Output (Classify-Value $_)
    }

    $now = Get-Date -Format 'o'
    Write-Host "now=$now name=$Name"
}
catch {
    Write-Error "failure: $($_.Exception.Message)"
}
finally {
    Write-Host 'done'
}
