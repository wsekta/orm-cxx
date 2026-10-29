param(
    [Parameter(Mandatory = $true)][string]$ProfilePath,
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'
$exitCode = 0
$startupMutex = $null
$ownsMutex = $false
$transcriptStarted = $false
$startSession = $null

function Test-SharedListener([string]$Address) {
    $response = $null
    try {
        $request = [Net.HttpWebRequest]::Create($Address)
        $request.Proxy = $null
        $request.AllowAutoRedirect = $false
        $request.Timeout = 2000
        $response = $request.GetResponse()
        return ([int]$response.StatusCode -eq 200)
    } catch {
        return $false
    } finally {
        if ($null -ne $response) { $response.Close() }
    }
}

function Get-PrivateServers([string]$DesktopPath) {
    $desktopProcesses = @(Get-CimInstance Win32_Process -Filter "Name='ChatGPT.exe'" |
        Where-Object { $_.ExecutablePath -eq $DesktopPath })
    if ($desktopProcesses.Count -eq 0) { return }
    Get-CimInstance Win32_Process -Filter "Name='codex.exe'" | Where-Object {
        $_.CommandLine -match '\bapp-server\b' -and
        $_.ParentProcessId -in $desktopProcesses.ProcessId
    }
}

try {
    $launchProfile = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($launchProfile.schemaVersion -ne 1) { throw 'Nieobslugiwana wersja profilu launchera.' }
    foreach ($property in @('launcherDirectory', 'nodePath', 'pythonPath', 'bridgeConfig', 'stateRoot')) {
        if ($launchProfile.$property -isnot [string] -or -not [IO.Path]::IsPathRooted($launchProfile.$property)) {
            throw ('Brak bezwzglednej sciezki profilu: ' + $property)
        }
    }
    if ($launchProfile.thread -notmatch '^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$') {
        throw 'Nieprawidlowy identyfikator rozmowy w profilu.'
    }
    if ($launchProfile.appServer -notmatch '^ws://(127\.0\.0\.1|\[::1\]):([1-9][0-9]{0,4})/?$') {
        throw 'Launcher wymaga lokalnego adresu WebSocket.'
    }
    $serverUri = [Uri]$launchProfile.appServer
    if ($serverUri.Port -gt 65535) { throw 'Nieprawidlowy port serwera.' }
    $readyAddress = 'http://' + $serverUri.Authority + '/readyz'
    $startSession = Join-Path $launchProfile.stateRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $startSession -Force | Out-Null
    Start-Transcript -LiteralPath (Join-Path $startSession 'start.log') | Out-Null
    $transcriptStarted = $true
    Write-Host 'ChatGPT z webhookami' -ForegroundColor Cyan
    Write-Host ('Log tej proby: ' + (Join-Path $startSession 'start.log'))

    $userSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $startupMutex = [Threading.Mutex]::new($false, ('Local\CodexCiWebhook-DesktopStart-' + $userSid))
    try { $ownsMutex = $startupMutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (-not $ownsMutex) { throw 'Inna proba uruchomienia juz trwa. Wroc do poprzedniego okna.' }

    $preparePath = Join-Path $launchProfile.launcherDirectory 'desktop\prepare.ps1'
    $launcherPath = Join-Path $launchProfile.launcherDirectory 'desktop\launch.mjs'
    foreach ($requiredFile in @($preparePath, $launcherPath, $launchProfile.nodePath, $launchProfile.pythonPath, $launchProfile.bridgeConfig)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) { throw ('Brak wymaganego pliku: ' + $requiredFile) }
    }

    if (Test-SharedListener $readyAddress) {
        Write-Host 'Lokalny serwer juz odpowiada. Nie uruchamiam drugiego launchera ani restartu.' -ForegroundColor Yellow
        Write-Host 'Otworz rozmowe i popros agenta o potwierdzenie webhookow oraz stanu bridge.'
    } else {
        $livePilots = @(Get-ScheduledTask | Where-Object {
            $_.TaskName -like 'CodexCiWebhook-DesktopPilot-*' -and [string]$_.State -in @('Running', 'Queued')
        })
        if ($livePilots.Count -gt 0) { throw 'Poprzedni launcher nadal pracuje. Nie rozpoczynam drugiej proby.' }

        $packages = @(Get-AppxPackage 'OpenAI.Codex')
        if ($packages.Count -ne 1) { throw 'Nie znaleziono jednej zainstalowanej aplikacji ChatGPT/Codex dla tego uzytkownika.' }
        $desktopPath = Join-Path $packages[0].InstallLocation 'app\ChatGPT.exe'
        if (-not (Test-Path -LiteralPath $desktopPath -PathType Leaf)) { throw 'Nie znaleziono pliku aplikacji.' }
        $currentSessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
        $otherVersion = @(Get-CimInstance Win32_Process -Filter "Name='ChatGPT.exe'" | Where-Object {
            $_.SessionId -eq $currentSessionId -and $_.ExecutablePath -match '\\OpenAI\.Codex_[^\\]+\\app\\ChatGPT\.exe$' -and
            $_.ExecutablePath -ne $desktopPath
        })
        if ($otherVersion.Count -gt 0) { throw 'Dziala jeszcze inna wersja aplikacji. Nie uruchamiam drugiej wersji; zachowaj ten log.' }
        $desktopProcesses = @(Get-CimInstance Win32_Process -Filter "Name='ChatGPT.exe'" |
            Where-Object { $_.ExecutablePath -eq $desktopPath })
        if ($desktopProcesses.Count -eq 0) {
            Write-Host 'Otwieram aplikacje...'
            Start-Process -FilePath $desktopPath -ArgumentList ('codex://threads/' + $launchProfile.thread + '?hostId=local') | Out-Null
        }

        Write-Host 'Czekam na gotowosc zwyklej aplikacji...'
        $readyDeadline = [DateTime]::UtcNow.AddSeconds(60)
        do {
            $privateServers = @(Get-PrivateServers $desktopPath)
            if ($privateServers.Count -gt 1) { throw 'Wykryto kilka serwerow aplikacji. Zachowaj log; niczego nie zamykam.' }
            if ($privateServers.Count -eq 1) { break }
            Start-Sleep -Milliseconds 500
        } while ([DateTime]::UtcNow -lt $readyDeadline)
        if ($privateServers.Count -ne 1) { throw 'Aplikacja nie przygotowala serwera w ciagu 60 sekund. Zachowaj to okno i log.' }

        $ignoredTurns = @($launchProfile.ignoreBusyTurns)
        foreach ($turnId in $ignoredTurns) {
            if ($turnId -notmatch '^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$') {
                throw 'Profil zawiera nieprawidlowy identyfikator historycznej tury.'
            }
        }
        $preparation = @{
            BridgeConfig = $launchProfile.bridgeConfig
            Thread = $launchProfile.thread
            PythonPath = $launchProfile.pythonPath
            NodePath = $launchProfile.nodePath
            StateDirectory = $startSession
            AppServer = $launchProfile.appServer
            IgnoreBusyTurns = $ignoredTurns
        }
        Write-Host 'Przygotowuje swieza konfiguracje. Historia rozmowy pozostaje w tej samej rozmowie.'
        $preparationOutput = @(& $preparePath @preparation)
        $preparedConfig = [string]($preparationOutput | Select-Object -Last 1)
        if (-not $preparedConfig -or -not (Test-Path -LiteralPath $preparedConfig -PathType Leaf)) {
            throw 'Nie powstal nowy plik konfiguracji. Nie uzywam poprzedniego pliku.'
        }
        [IO.File]::WriteAllText((Join-Path $launchProfile.stateRoot 'last-session.txt'), $startSession, [Text.UTF8Encoding]::new($false))
        Write-Host 'Czekam na koniec aktywnych rozmow i kontrolowany restart aplikacji...'
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $armOutput = @(& $launchProfile.nodePath $launcherPath --config $preparedConfig --arm 2>&1)
            $armExitCode = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previousPreference }
        foreach ($line in $armOutput) { Write-Host ([string]$line) }
        if ($armExitCode -ne 0) { throw 'Launcher nie potwierdzil startu. Szczegoly sa w logu tej proby.' }

        $statusPath = Join-Path $startSession 'status.json'
        $startupDeadline = [DateTime]::UtcNow.AddMinutes(8)
        $lastPhase = $null
        $verifiedStart = $false
        do {
            $startupStatus = $null
            if (Test-Path -LiteralPath $statusPath) {
                try { $startupStatus = Get-Content -LiteralPath $statusPath -Raw -Encoding UTF8 | ConvertFrom-Json }
                catch { $startupStatus = $null }
            }
            if ($null -ne $startupStatus) {
                if ($startupStatus.phase -ne $lastPhase) {
                    $lastPhase = $startupStatus.phase
                    Write-Host ('Etap: ' + $lastPhase)
                }
                if ($startupStatus.phase -in @('awaiting-conversation-verification', 'active')) {
                    if (-not $startupStatus.historyVerified) { throw 'Launcher nie potwierdzil zachowania historii.' }
                    $verifiedStart = $true
                    break
                }
                if ($startupStatus.phase -in @('rolled-back', 'rollback-blocked', 'aborted-before-restart', 'desktop-exited')) {
                    throw ('Proba zakonczona na etapie ' + $startupStatus.phase + '. ' + $startupStatus.error)
                }
            }
            Start-Sleep -Seconds 1
        } while ([DateTime]::UtcNow -lt $startupDeadline)
        if (-not $verifiedStart) { throw 'Nie otrzymano potwierdzenia startu w ciagu 8 minut. Nie ponawiaj proby; zachowaj log i sprawdz stan launchera.' }
        Write-Host 'Aplikacja zostala uruchomiona z lokalnym polaczeniem. Wroc do tej samej rozmowy.' -ForegroundColor Green
        Write-Host 'Oczekuj wiadomosci kontrolnej. Agent potwierdzi narzedzia, wybudzenie i uruchomi bridge.'
        Write-Host 'Samo potwierdzenie kolejki nie oznacza jeszcze dzialajacych webhookow.'
    }
} catch {
    $exitCode = 1
    $failureRecord = ($_ | Out-String)
    Write-Host 'Nie udalo sie uruchomic trybu webhookow.' -ForegroundColor Red
    Write-Host $failureRecord -ForegroundColor Red
    if ($startSession) {
        try { [IO.File]::WriteAllText((Join-Path $startSession 'error.txt'), $failureRecord, [Text.UTF8Encoding]::new($false)) }
        catch { Write-Host 'Nie mozna zapisac pliku bledu. Skopiuj komunikat z tego okna.' -ForegroundColor Yellow }
        Write-Host ('Przekaz agentowi plik: ' + (Join-Path $startSession 'start.log'))
    }
} finally {
    if ($transcriptStarted) { try { Stop-Transcript | Out-Null } catch { } }
    if ($ownsMutex) { $startupMutex.ReleaseMutex() }
    if ($null -ne $startupMutex) { $startupMutex.Dispose() }
}

if (-not $NoPause) { Read-Host 'Nacisnij Enter, aby zamknac to okno' }
exit $exitCode
