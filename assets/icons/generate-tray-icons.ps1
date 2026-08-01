[CmdletBinding()]
param(
    [switch] $VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$traySizes = @(16, 20, 24, 32, 40, 48, 64)
$appSizes = @(16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 128, 256)
$loadingFrameCount = 12
$app = @{
    Accent = '#A9623D'
    Glyph = 'microphone-filled'
}
$states = [ordered]@{
    'loading' = @{
        Accent = '#3478D4'
        Glyph = 'progress-ring'
    }
    'waiting' = @{
        Accent = '#707783'
        Glyph = 'microphone'
    }
    'listening' = @{
        Accent = '#168A55'
        Glyph = 'microphone-filled'
    }
    'follow-up' = @{
        Accent = '#0E806C'
        Glyph = 'microphone-ring'
    }
    'thinking' = @{
        Accent = '#A7650B'
        Glyph = 'ellipsis'
    }
    'speaking' = @{
        Accent = '#3478D4'
        Glyph = 'audio-bars'
    }
    'reminder' = @{
        Accent = '#8065B5'
        Glyph = 'bell'
    }
    'paused' = @{
        Accent = '#707783'
        Glyph = 'microphone-off'
    }
    'error' = @{
        Accent = '#C84955'
        Glyph = 'error-circle'
    }
}

function Convert-HexColor([string] $hex) {
    return [System.Drawing.ColorTranslator]::FromHtml($hex)
}

function New-RoundPen(
    [System.Drawing.Color] $color,
    [single] $width
) {
    $pen = [System.Drawing.Pen]::new($color, $width)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    return $pen
}

function New-MicrophonePath {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $x = [single]5.35
    $y = [single]2.0
    $width = [single]5.3
    $height = [single]7.9
    $diameter = [single]5.3
    $path.AddArc($x, $y, $diameter, $diameter, 180, 90)
    $path.AddArc(
        $x + $width - $diameter,
        $y,
        $diameter,
        $diameter,
        270,
        90)
    $path.AddArc(
        $x + $width - $diameter,
        $y + $height - $diameter,
        $diameter,
        $diameter,
        0,
        90)
    $path.AddArc(
        $x,
        $y + $height - $diameter,
        $diameter,
        $diameter,
        90,
        90)
    $path.CloseFigure()
    return $path
}

function Draw-Microphone(
    [System.Drawing.Graphics] $graphics,
    [System.Drawing.Color] $color,
    [switch] $Filled,
    [switch] $Ring,
    [switch] $Slashed
) {
    $body = New-MicrophonePath
    $pen = New-RoundPen $color 1.35
    $brush = [System.Drawing.SolidBrush]::new($color)
    try {
        if ($Filled) {
            $graphics.FillPath($brush, $body)
        }
        else {
            $graphics.DrawPath($pen, $body)
        }
        $graphics.DrawArc(
            $pen,
            [single]3.45,
            [single]5.15,
            [single]9.1,
            [single]7.35,
            0,
            180)
        $graphics.DrawLine(
            $pen,
            [single]8.0,
            [single]12.3,
            [single]8.0,
            [single]13.65)
        $graphics.DrawLine(
            $pen,
            [single]5.65,
            [single]13.65,
            [single]10.35,
            [single]13.65)
    }
    finally {
        $brush.Dispose()
        $pen.Dispose()
        $body.Dispose()
    }

    if ($Ring) {
        $ringPen = New-RoundPen $color 0.85
        try {
            $ringPen.DashStyle =
                [System.Drawing.Drawing2D.DashStyle]::Dot
            $ringPen.DashCap =
                [System.Drawing.Drawing2D.DashCap]::Round
            $graphics.DrawEllipse(
                $ringPen,
                [single]2.35,
                [single]1.55,
                [single]11.3,
                [single]12.15)
        }
        finally {
            $ringPen.Dispose()
        }
    }

    if ($Slashed) {
        $erasePen = New-RoundPen (
            [System.Drawing.Color]::Transparent
        ) 2.8
        $slashPen = New-RoundPen $color 1.55
        try {
            $graphics.CompositingMode =
                [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.DrawLine(
                $erasePen,
                [single]3.1,
                [single]2.9,
                [single]12.9,
                [single]13.1)
            $graphics.CompositingMode =
                [System.Drawing.Drawing2D.CompositingMode]::SourceOver
            $graphics.DrawLine(
                $slashPen,
                [single]3.1,
                [single]2.9,
                [single]12.9,
                [single]13.1)
        }
        finally {
            $graphics.CompositingMode =
                [System.Drawing.Drawing2D.CompositingMode]::SourceOver
            $erasePen.Dispose()
            $slashPen.Dispose()
        }
    }
}

function Draw-Glyph(
    [System.Drawing.Graphics] $graphics,
    [string] $glyph,
    [System.Drawing.Color] $color,
    [single] $phase = 0.0
) {
    switch ($glyph) {
        'progress-ring' {
            $pen = New-RoundPen $color 1.8
            try {
                $graphics.DrawArc(
                    $pen,
                    [single]2.45,
                    [single]2.45,
                    [single]11.1,
                    [single]11.1,
                    [single](205.0 + $phase),
                    255)
            }
            finally {
                $pen.Dispose()
            }
        }
        'microphone' {
            Draw-Microphone $graphics $color
        }
        'microphone-filled' {
            Draw-Microphone $graphics $color -Filled
        }
        'microphone-ring' {
            Draw-Microphone $graphics $color -Ring
        }
        'microphone-off' {
            Draw-Microphone $graphics $color -Slashed
        }
        'ellipsis' {
            $brush = [System.Drawing.SolidBrush]::new($color)
            try {
                foreach ($x in @([single]2.4, [single]6.8, [single]11.2)) {
                    $graphics.FillEllipse(
                        $brush,
                        $x,
                        [single]6.75,
                        [single]2.4,
                        [single]2.4)
                }
            }
            finally {
                $brush.Dispose()
            }
        }
        'audio-bars' {
            $pen = New-RoundPen $color 1.35
            try {
                foreach ($bar in @(
                    @([single]2.8, [single]6.3, [single]9.7),
                    @([single]5.4, [single]4.5, [single]11.5),
                    @([single]8.0, [single]2.65, [single]13.35),
                    @([single]10.6, [single]4.5, [single]11.5),
                    @([single]13.2, [single]6.3, [single]9.7)
                )) {
                    $graphics.DrawLine(
                        $pen,
                        $bar[0],
                        $bar[1],
                        $bar[0],
                        $bar[2])
                }
            }
            finally {
                $pen.Dispose()
            }
        }
        'bell' {
            $pen = New-RoundPen $color 1.45
            $brush = [System.Drawing.SolidBrush]::new($color)
            $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
            try {
                $path.AddBezier(
                    [single]3.35, [single]10.65,
                    [single]4.55, [single]9.25,
                    [single]4.25, [single]7.6,
                    [single]4.6, [single]6.0)
                $path.AddBezier(
                    [single]4.6, [single]6.0,
                    [single]5.0, [single]3.95,
                    [single]6.2, [single]2.75,
                    [single]8.0, [single]2.75)
                $path.AddBezier(
                    [single]8.0, [single]2.75,
                    [single]9.8, [single]2.75,
                    [single]11.0, [single]3.95,
                    [single]11.4, [single]6.0)
                $path.AddBezier(
                    [single]11.4, [single]6.0,
                    [single]11.75, [single]7.6,
                    [single]11.45, [single]9.25,
                    [single]12.65, [single]10.65)
                $path.AddLine(
                    [single]12.65,
                    [single]10.65,
                    [single]3.35,
                    [single]10.65)
                $graphics.DrawPath($pen, $path)
                $graphics.DrawLine(
                    $pen,
                    [single]5.45,
                    [single]12.0,
                    [single]10.55,
                    [single]12.0)
                $graphics.FillEllipse(
                    $brush,
                    [single]7.25,
                    [single]13.0,
                    [single]1.5,
                    [single]1.5)
            }
            finally {
                $path.Dispose()
                $brush.Dispose()
                $pen.Dispose()
            }
        }
        'error-circle' {
            $brush = [System.Drawing.SolidBrush]::new($color)
            $mark = [System.Drawing.Color]::FromArgb(
                255,
                247,
                247,
                249)
            $pen = New-RoundPen $mark 1.55
            try {
                $graphics.FillEllipse(
                    $brush,
                    [single]2.25,
                    [single]2.25,
                    [single]11.5,
                    [single]11.5)
                $graphics.DrawLine(
                    $pen,
                    [single]8.0,
                    [single]5.0,
                    [single]8.0,
                    [single]9.25)
                $graphics.DrawLine(
                    $pen,
                    [single]8.0,
                    [single]11.35,
                    [single]8.0,
                    [single]11.45)
            }
            finally {
                $pen.Dispose()
                $brush.Dispose()
            }
        }
        default {
            throw "Unknown icon glyph: $glyph"
        }
    }
}

function New-Frame(
    [int] $size,
    [string] $glyph,
    [System.Drawing.Color] $accent,
    [single] $phase = 0.0
) {
    $renderSize = $size * 4
    $render = [System.Drawing.Bitmap]::new(
        $renderSize,
        $renderSize,
        [System.Drawing.Imaging.PixelFormat]::Format32bppPArgb)
    $renderGraphics = [System.Drawing.Graphics]::FromImage($render)
    try {
        $renderGraphics.Clear(
            [System.Drawing.Color]::Transparent)
        $renderGraphics.SmoothingMode =
            [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $renderGraphics.PixelOffsetMode =
            [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $renderGraphics.ScaleTransform(
            [single]($renderSize / 16.0),
            [single]($renderSize / 16.0))
        Draw-Glyph $renderGraphics $glyph $accent $phase
    }
    finally {
        $renderGraphics.Dispose()
    }

    $bitmap = [System.Drawing.Bitmap]::new(
        $size,
        $size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode =
            [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.CompositingQuality =
            [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode =
            [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode =
            [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.DrawImage(
            $render,
            [System.Drawing.Rectangle]::new(0, 0, $size, $size),
            0,
            0,
            $renderSize,
            $renderSize,
            [System.Drawing.GraphicsUnit]::Pixel)
    }
    finally {
        $graphics.Dispose()
        $render.Dispose()
    }
    return $bitmap
}

function Write-Ico([string] $path, [object[]] $frames) {
    $payloads = foreach ($frame in $frames) {
        $stream = [System.IO.MemoryStream]::new()
        try {
            $frame.Bitmap.Save(
                $stream,
                [System.Drawing.Imaging.ImageFormat]::Png)
            , $stream.ToArray()
        }
        finally {
            $stream.Dispose()
        }
    }

    $file = [System.IO.File]::Open(
        $path,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None)
    $writer = [System.IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$frames.Count)
        $offset = 6 + (16 * $frames.Count)
        for ($index = 0; $index -lt $frames.Count; $index++) {
            $size = [int]$frames[$index].Size
            $encodedSize = if ($size -eq 256) { 0 } else { $size }
            $payload = [byte[]]$payloads[$index]
            $writer.Write([byte]$encodedSize)
            $writer.Write([byte]$encodedSize)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$payload.Length)
            $writer.Write([uint32]$offset)
            $offset += $payload.Length
        }
        foreach ($payload in $payloads) {
            $writer.Write([byte[]]$payload)
        }
    }
    finally {
        $writer.Dispose()
        $file.Dispose()
    }
}

function Get-Luminance([System.Drawing.Color] $color) {
    $channels = @($color.R, $color.G, $color.B) | ForEach-Object {
        $value = $_ / 255.0
        if ($value -le 0.04045) {
            $value / 12.92
        }
        else {
            [Math]::Pow(($value + 0.055) / 1.055, 2.4)
        }
    }
    return (
        (0.2126 * $channels[0]) +
        (0.7152 * $channels[1]) +
        (0.0722 * $channels[2]))
}

function Get-Contrast(
    [System.Drawing.Color] $first,
    [System.Drawing.Color] $second
) {
    $one = Get-Luminance $first
    $two = Get-Luminance $second
    return ([Math]::Max($one, $two) + 0.05) /
        ([Math]::Min($one, $two) + 0.05)
}

function Read-Ico([string] $path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 6 -or
        [BitConverter]::ToUInt16($bytes, 0) -ne 0 -or
        [BitConverter]::ToUInt16($bytes, 2) -ne 1) {
        throw "$path is not an ICO file."
    }
    $count = [BitConverter]::ToUInt16($bytes, 4)
    $result = @()
    for ($index = 0; $index -lt $count; $index++) {
        $entry = 6 + (16 * $index)
        $width = if ($bytes[$entry] -eq 0) { 256 } else { $bytes[$entry] }
        $height = if ($bytes[$entry + 1] -eq 0) {
            256
        }
        else {
            $bytes[$entry + 1]
        }
        $length = [BitConverter]::ToUInt32($bytes, $entry + 8)
        $offset = [BitConverter]::ToUInt32($bytes, $entry + 12)
        if ($offset + $length -gt $bytes.Length) {
            throw "$path contains an out-of-range frame."
        }
        $stream = [System.IO.MemoryStream]::new(
            $bytes,
            [int]$offset,
            [int]$length,
            $false)
        try {
            $image = [System.Drawing.Image]::FromStream($stream)
            try {
                $bitmap = [System.Drawing.Bitmap]::new($image)
            }
            finally {
                $image.Dispose()
            }
        }
        finally {
            $stream.Dispose()
        }
        $result += [pscustomobject]@{
            Width = $width
            Height = $height
            Bitmap = $bitmap
        }
    }
    return $result
}

function Test-Ico(
    [string] $path,
    [int[]] $expectedSizes
) {
    $frames = Read-Ico $path
    try {
        $actualSizes = @($frames | ForEach-Object Width)
        if ($frames.Count -ne $expectedSizes.Count -or
            (Compare-Object $expectedSizes $actualSizes)) {
            throw "$path does not contain the required frame set."
        }

        $metrics = foreach ($frame in $frames) {
            if ($frame.Width -ne $frame.Height -or
                $frame.Bitmap.Width -ne $frame.Width -or
                $frame.Bitmap.Height -ne $frame.Height) {
                throw "$path contains a non-square frame."
            }
            $minX = $frame.Width
            $minY = $frame.Height
            $maxX = -1
            $maxY = -1
            $opaque = 0
            $solid = 0
            $nearWhite = 0
            $alphaLevels =
                [System.Collections.Generic.HashSet[int]]::new()
            for ($y = 0; $y -lt $frame.Height; $y++) {
                for ($x = 0; $x -lt $frame.Width; $x++) {
                    $pixel = $frame.Bitmap.GetPixel($x, $y)
                    [void]$alphaLevels.Add([int]$pixel.A)
                    if ($pixel.A -ne 0) {
                        $opaque++
                        if ($pixel.A -ge 240) {
                            $solid++
                        }
                        if ($pixel.R -ge 240 -and
                            $pixel.G -ge 240 -and
                            $pixel.B -ge 240) {
                            $nearWhite++
                        }
                        $minX = [Math]::Min($minX, $x)
                        $minY = [Math]::Min($minY, $y)
                        $maxX = [Math]::Max($maxX, $x)
                        $maxY = [Math]::Max($maxY, $y)
                    }
                }
            }
            if ($frame.Width -eq 16 -and
                ($minX -lt 1 -or $minY -lt 1 -or
                 $maxX -gt 14 -or $maxY -gt 14)) {
                throw "$path violates the 1 px safety margin."
            }
            if ($frame.Width -eq 16 -and
                $solid -gt 128) {
                throw "$path is too tile-like at 16 px."
            }
            if ($frame.Width -eq 16 -and
                $nearWhite -gt [Math]::Max(4, $opaque / 4)) {
                throw "$path contains a white plate at 16 px."
            }
            [pscustomobject]@{
                Size = $frame.Width
                Bounds = "$minX,$minY-$maxX,$maxY"
                Opaque = $opaque
                Solid = $solid
                AlphaLevels = $alphaLevels.Count
            }
        }
        return $metrics
    }
    finally {
        foreach ($frame in $frames) {
            $frame.Bitmap.Dispose()
        }
    }
}

function Get-IcoFrameHash(
    [string] $path,
    [int] $size
) {
    $frames = Read-Ico $path
    try {
        $frame = $frames |
            Where-Object Width -eq $size |
            Select-Object -First 1
        if ($null -eq $frame) {
            throw "$path has no $size px frame."
        }
        $stream = [System.IO.MemoryStream]::new()
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            $frame.Bitmap.Save(
                $stream,
                [System.Drawing.Imaging.ImageFormat]::Png)
            return [Convert]::ToBase64String(
                $sha.ComputeHash($stream.ToArray()))
        }
        finally {
            $sha.Dispose()
            $stream.Dispose()
        }
    }
    finally {
        foreach ($frame in $frames) {
            $frame.Bitmap.Dispose()
        }
    }
}

$glyphSignatures = $states.Values |
    ForEach-Object { $_.Glyph } |
    Sort-Object -Unique
if ($glyphSignatures.Count -ne $states.Count) {
    throw 'Every tray state must have a shape-distinct glyph.'
}

if (-not $VerifyOnly) {
    $accent = Convert-HexColor $app.Accent
    $frames = foreach ($size in $appSizes) {
        [pscustomobject]@{
            Size = $size
            Bitmap = New-Frame $size $app.Glyph $accent
        }
    }
    try {
        Write-Ico (Join-Path $PSScriptRoot 'app.ico') $frames
    }
    finally {
        foreach ($frame in $frames) {
            $frame.Bitmap.Dispose()
        }
    }

    foreach ($name in $states.Keys) {
        $definition = $states[$name]
        $accent = Convert-HexColor $definition.Accent
        $frames = foreach ($size in $traySizes) {
            [pscustomobject]@{
                Size = $size
                Bitmap = New-Frame (
                    $size
                ) $definition.Glyph $accent
            }
        }
        try {
            Write-Ico (
                Join-Path $PSScriptRoot "tray-$name.ico"
            ) $frames
        }
        finally {
            foreach ($frame in $frames) {
                $frame.Bitmap.Dispose()
            }
        }
    }

    $loading = $states['loading']
    $accent = Convert-HexColor $loading.Accent
    for ($index = 1; $index -lt $loadingFrameCount; $index++) {
        $phase = [single](360.0 * $index / $loadingFrameCount)
        $frames = foreach ($size in $traySizes) {
            [pscustomobject]@{
                Size = $size
                Bitmap = New-Frame (
                    $size
                ) $loading.Glyph $accent $phase
            }
        }
        try {
            Write-Ico (
                Join-Path $PSScriptRoot (
                    'tray-loading-{0:D2}.ico' -f $index)
            ) $frames
        }
        finally {
            foreach ($frame in $frames) {
                $frame.Bitmap.Dispose()
            }
        }
    }
}

$lightTaskbar = Convert-HexColor '#F3F3F3'
$darkTaskbar = Convert-HexColor '#202020'

$appMetrics = Test-Ico (
    (Join-Path $PSScriptRoot 'app.ico')
) $appSizes

$loadingPaths = @(
    Join-Path $PSScriptRoot 'tray-loading.ico'
)
for ($index = 1; $index -lt $loadingFrameCount; $index++) {
    $loadingPaths += Join-Path $PSScriptRoot (
        'tray-loading-{0:D2}.ico' -f $index)
}
$loadingHashes16 = @()
$loadingHashes20 = @()
foreach ($path in $loadingPaths) {
    [void](Test-Ico $path $traySizes)
    $loadingHashes16 += Get-IcoFrameHash $path 16
    $loadingHashes20 += Get-IcoFrameHash $path 20
}
if (($loadingHashes16 | Sort-Object -Unique).Count -ne
        $loadingFrameCount -or
    ($loadingHashes20 | Sort-Object -Unique).Count -ne
        $loadingFrameCount) {
    throw 'Loading animation frames must be pixel-distinct at 16 and 20 px.'
}

$report = foreach ($name in $states.Keys) {
    $definition = $states[$name]
    $accent = Convert-HexColor $definition.Accent
    $lightContrast = Get-Contrast $accent $lightTaskbar
    $darkContrast = Get-Contrast $accent $darkTaskbar
    if ($lightContrast -lt 3.0 -or
        $darkContrast -lt 3.0) {
        throw "$name fails the 3:1 taskbar contrast floor."
    }
    $metrics = Test-Ico (
        (Join-Path $PSScriptRoot "tray-$name.ico")
    ) $traySizes
    $frame16 = $metrics | Where-Object Size -eq 16
    $frame20 = $metrics | Where-Object Size -eq 20
    [pscustomobject]@{
        State = $name
        Frames = ($metrics.Size -join '/')
        Bounds16 = $frame16.Bounds
        Opaque16 = $frame16.Opaque
        Bounds20 = $frame20.Bounds
        Opaque20 = $frame20.Opaque
        Alpha16 = $frame16.AlphaLevels
        LightTaskbar = [Math]::Round($lightContrast, 2)
        DarkTaskbar = [Math]::Round($darkContrast, 2)
    }
}

[pscustomobject]@{
    State = 'app'
    Frames = ($appMetrics.Size -join '/')
    Bounds16 = ($appMetrics |
        Where-Object Size -eq 16).Bounds
    Opaque16 = ($appMetrics |
        Where-Object Size -eq 16).Opaque
    Bounds20 = ($appMetrics |
        Where-Object Size -eq 20).Bounds
    Opaque20 = ($appMetrics |
        Where-Object Size -eq 20).Opaque
    Alpha16 = ($appMetrics |
        Where-Object Size -eq 16).AlphaLevels
    LightTaskbar = [Math]::Round(
        (Get-Contrast (
            (Convert-HexColor $app.Accent)
        ) $lightTaskbar),
        2)
    DarkTaskbar = [Math]::Round(
        (Get-Contrast (
            (Convert-HexColor $app.Accent)
        ) $darkTaskbar),
        2)
} | Format-Table -AutoSize
$report | Format-Table -AutoSize
Write-Host (
    'Loading animation: {0} unique frames at 16/20 px.' -f
        $loadingFrameCount)
