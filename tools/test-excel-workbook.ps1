<#
.SYNOPSIS
    Check plugin reports using a separate installed Excel instance, without repair.
.DESCRIPTION
    Optional Windows-only compatibility test. Opens reports read-only with
    xlNormalLoad, disables macros/events/links, and never saves a workbook.
    Does not connect to or close an existing user Excel instance.
#>
[CmdletBinding()]
param(
    [string[]]$Path = @((Join-Path $PSScriptRoot '..\.build\export-2.1.0-fixture\exports\报告.xlsx'))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $Path.Count) { throw 'Specify at least one workbook.' }
$workbookPaths = @($Path | ForEach-Object {
    $item = Get-Item -LiteralPath $_
    if ($item.PSIsContainer -or $item.Extension -ine '.xlsx') {
        throw "Expected an XLSX file: $_"
    }
    $item.FullName
})

$excel = $null
$books = $null
try {
    $excel = New-Object -ComObject Excel.Application
    $books = $excel.Workbooks
    if ($books.Count -ne 0) { throw 'Expected a separate Excel instance without workbooks.' }
    $excel.Visible = $false
    $excel.DisplayAlerts = $false
    $excel.AskToUpdateLinks = $false
    $excel.EnableEvents = $false
    $excel.AutomationSecurity = 3 # msoAutomationSecurityForceDisable
    Write-Host "Excel $($excel.Version), build $($excel.Build); normal load, read-only, no repair"

    foreach ($workbookPath in $workbookPaths) {
        $book = $null
        try {
            # UpdateLinks=0, ReadOnly=true. Omitted CorruptLoad defaults to
            # xlNormalLoad; never request xlRepairFile or save the workbook.
            $book = $books.Open($workbookPath, 0, $true)
            if ($null -eq $book) { throw "Excel did not open $workbookPath" }
            if (-not $book.ReadOnly) { throw 'Expected a read-only workbook.' }
            $sheets = $book.Worksheets
            try {
                if ($sheets.Count -lt 1) { throw 'Workbook contains no worksheets.' }
                Write-Host "PASS $([IO.Path]::GetFileName($workbookPath)): $($sheets.Count) worksheets"
            } finally {
                [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($sheets)
            }
        } finally {
            if ($null -ne $book) {
                try { $book.Close($false) } finally {
                    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($book)
                }
            }
        }
    }
} finally {
    try {
        if ($null -ne $books) {
            [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($books)
        }
    } finally {
        if ($null -ne $excel) {
            try { $excel.Quit() } finally {
                [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($excel)
            }
        }
    }
}
