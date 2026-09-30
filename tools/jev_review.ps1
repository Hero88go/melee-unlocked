[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$StateFile,

    [Parameter(Mandatory = $true)]
    [string]$QuestionsFile,

    [string]$CacheDir = "run-source\jev-cache",
    [string]$Model = "jev-1.13.0",
    [switch]$DryRun,
    [switch]$NoCache
)

$ErrorActionPreference = "Stop"

function Get-Sha256Hex([string]$Text) {
    $hash = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return (-join ($hash.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") }))
    } finally {
        $hash.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $StateFile -PathType Leaf)) {
    throw "State file not found: $StateFile"
}
if (-not (Test-Path -LiteralPath $QuestionsFile -PathType Leaf)) {
    throw "Questions file not found: $QuestionsFile"
}

$state = Get-Content -LiteralPath $StateFile -Raw
$questionsText = Get-Content -LiteralPath $QuestionsFile -Raw
$questions = $questionsText | ConvertFrom-Json
$request = [ordered]@{
    state = $state
    model = $Model
    questions = $questions
}
$canonical = $request | ConvertTo-Json -Depth 50 -Compress
$cacheKey = Get-Sha256Hex $canonical
$cachePath = Join-Path -Path $CacheDir -ChildPath "$cacheKey.json"

if ($DryRun) {
    [ordered]@{
        endpoint = "https://api.typesafe.ai/v1/systemone"
        model = $Model
        cache_key = $cacheKey
        state_characters = $state.Length
        question_names = @($questions.PSObject.Properties.Name)
        cached = Test-Path -LiteralPath $cachePath -PathType Leaf
        request = $request
    } | ConvertTo-Json -Depth 50
    exit 0
}

if (-not $NoCache -and (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    Get-Content -LiteralPath $cachePath -Raw
    exit 0
}

$apiKey = [Environment]::GetEnvironmentVariable("TYPESAFE_API_KEY")
if ([string]::IsNullOrWhiteSpace($apiKey)) {
    throw "TYPESAFE_API_KEY is not set"
}

$headers = @{
    Authorization = "Bearer $apiKey"
    "Content-Type" = "application/json"
}
try {
    $response = Invoke-RestMethod -Uri "https://api.typesafe.ai/v1/systemone" -Method Post -Headers $headers -UserAgent "curl/8.10.1" -Body ([System.Text.Encoding]::UTF8.GetBytes($canonical)) -TimeoutSec 20
    New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
    $json = $response | ConvertTo-Json -Depth 50
    Set-Content -LiteralPath $cachePath -Value $json -Encoding UTF8
    $json
} finally {
    $apiKey = $null
    Remove-Variable apiKey -ErrorAction SilentlyContinue
}
