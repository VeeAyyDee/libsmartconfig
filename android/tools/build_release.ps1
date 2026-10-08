# SPDX-License-Identifier: 0BSD
param(
    [string]$Keystore = (Join-Path $env:USERPROFILE '.android/libsmartconfig-release.jks'),
    [string]$KeyAlias = 'libsmartconfig',
    [string]$JavaHome = $env:JAVA_HOME,
    [string]$SdkPath = $env:ANDROID_HOME,
    [switch]$UseDialog
)
$ErrorActionPreference = 'Stop'
$androidRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!(Test-Path -LiteralPath $Keystore -PathType Leaf)) { throw 'Release keystore not found.' }
if (!$JavaHome) { $JavaHome = Join-Path $env:ProgramFiles 'Android/Android Studio/jbr' }
if (!$SdkPath) { $SdkPath = Join-Path $env:LOCALAPPDATA 'Android/Sdk' }
$java = Join-Path $JavaHome 'bin/java.exe'
$signer = Join-Path $SdkPath 'build-tools/36.0.0/apksigner.bat'
$aapt = Join-Path $SdkPath 'build-tools/36.0.0/aapt.exe'
foreach ($tool in @($java, $signer, $aapt)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required build tool not found: $tool" }
}

$storeSecret = $null
$keySecret = $null
$names = @('JAVA_HOME', 'ANDROID_HOME', 'SMARTCONFIG_KEYSTORE', 'SMARTCONFIG_KEY_ALIAS',
    'SMARTCONFIG_STORE_PASSWORD', 'SMARTCONFIG_KEY_PASSWORD')
$savedEnvironment = @{}
foreach ($name in $names) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

try {
    if ($UseDialog) {
        # Local Windows UI: no password enters chat, command-line arguments, or files.
        Add-Type -AssemblyName System.Windows.Forms
        Add-Type -AssemblyName System.Drawing
        [System.Windows.Forms.Application]::EnableVisualStyles()
        $form = New-Object System.Windows.Forms.Form
        $form.Text = 'Sign SmartConfig Test APK'
        $form.ClientSize = New-Object System.Drawing.Size(510, 235)
        $form.StartPosition = 'CenterScreen'
        $form.FormBorderStyle = 'FixedDialog'
        $form.MaximizeBox = $false
        $form.MinimizeBox = $false
        $form.TopMost = $true
        $message = New-Object System.Windows.Forms.Label
        $message.SetBounds(18, 15, 475, 38)
        $message.Text = 'Enter the passwords for your release keystore. They stay in this local build process and are not saved.'
        $storeLabel = New-Object System.Windows.Forms.Label
        $storeLabel.SetBounds(18, 65, 200, 20)
        $storeLabel.Text = 'Keystore password'
        $storeBox = New-Object System.Windows.Forms.TextBox
        $storeBox.SetBounds(220, 61, 270, 26)
        $storeBox.UseSystemPasswordChar = $true
        $keyLabel = New-Object System.Windows.Forms.Label
        $keyLabel.SetBounds(18, 105, 200, 35)
        $keyLabel.Text = 'Key password (leave blank if same)'
        $keyBox = New-Object System.Windows.Forms.TextBox
        $keyBox.SetBounds(220, 107, 270, 26)
        $keyBox.UseSystemPasswordChar = $true
        $build = New-Object System.Windows.Forms.Button
        $build.SetBounds(285, 173, 100, 32)
        $build.Text = 'Build APK'
        $build.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $cancel = New-Object System.Windows.Forms.Button
        $cancel.SetBounds(395, 173, 95, 32)
        $cancel.Text = 'Cancel'
        $cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
        $form.Controls.AddRange(@($message, $storeLabel, $storeBox, $keyLabel, $keyBox, $build, $cancel))
        $form.AcceptButton = $build
        $form.CancelButton = $cancel
        $form.Add_Shown({ $storeBox.Focus() })
        try {
            if ($form.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { throw 'Signing canceled; no APK was signed.' }
            if (!$storeBox.Text) { throw 'Keystore password is required.' }
            $storeSecret = ConvertTo-SecureString -String $storeBox.Text -AsPlainText -Force
            if ($keyBox.Text) { $keySecret = ConvertTo-SecureString -String $keyBox.Text -AsPlainText -Force }
        } finally {
            $storeBox.Clear(); $keyBox.Clear(); $form.Dispose()
        }
    } else {
        $storeSecret = Read-Host 'Keystore password (not saved)' -AsSecureString
        $keySecret = Read-Host 'Key password (Enter if same)' -AsSecureString
    }
    if (!$storeSecret -or $storeSecret.Length -eq 0) { throw 'Keystore password is required.' }
    $env:JAVA_HOME = $JavaHome
    $env:ANDROID_HOME = $SdkPath
    $env:SMARTCONFIG_KEYSTORE = [IO.Path]::GetFullPath($Keystore)
    $env:SMARTCONFIG_KEY_ALIAS = $KeyAlias
    $env:SMARTCONFIG_STORE_PASSWORD = (New-Object System.Net.NetworkCredential('', $storeSecret)).Password
    $env:SMARTCONFIG_KEY_PASSWORD = if ($keySecret -and $keySecret.Length -gt 0) {
        (New-Object System.Net.NetworkCredential('', $keySecret)).Password
    } else { $env:SMARTCONFIG_STORE_PASSWORD }

    # No reusable daemon or configuration cache containing signing configuration.
    & (Join-Path $androidRoot 'gradlew.bat') -p $androidRoot --no-daemon --no-configuration-cache `
        :app:assembleRelease :app:lintRelease :codec:check --console=plain
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed. No distribution artifact was updated.' }

    $releaseDir = Join-Path $androidRoot 'app/build/outputs/apk/release'
    $metadata = Get-Content -LiteralPath (Join-Path $releaseDir 'output-metadata.json') -Raw | ConvertFrom-Json
    if ($metadata.elements.Count -ne 1) { throw 'Expected exactly one universal release APK.' }
    $version = $metadata.elements[0].versionName
    if ($version -notmatch '^[0-9A-Za-z][0-9A-Za-z._-]*$') { throw 'Unexpected release version format.' }
    $apk = Join-Path $releaseDir $metadata.elements[0].outputFile
    $certificate = & $signer verify --verbose --print-certs $apk 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed.' }
    $badging = & $aapt dump badging $apk 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'APK metadata verification failed.' }
    if ($badging -match 'application-debuggable') { throw 'Refusing to distribute a debuggable APK.' }

    $distribution = Join-Path $androidRoot 'dist'
    New-Item -ItemType Directory -Force $distribution | Out-Null
    $filename = "SmartConfig-Test-$version.apk"
    $destination = Join-Path $distribution $filename
    Copy-Item -LiteralPath $apk -Destination $destination -Force
    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath (Join-Path $distribution 'SHA256SUMS.txt') -Value "$hash  $filename" -Encoding Ascii
    $fingerprints = @($certificate | Where-Object { $_ -match '^Signer #\d+ certificate SHA-256 digest:' })
    if ($fingerprints.Count -eq 0) { throw 'APK verified but signing certificate fingerprint was unavailable.' }
    Set-Content -LiteralPath (Join-Path $distribution 'signing-certificate.txt') -Value $fingerprints -Encoding Ascii
    Write-Output "Signed, verified, non-debuggable APK: $destination"
    Write-Output "SHA-256: $hash"
    Write-Output $fingerprints
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    if ($storeSecret) { $storeSecret.Dispose() }
    if ($keySecret) { $keySecret.Dispose() }
    $savedEnvironment.Clear()
}
