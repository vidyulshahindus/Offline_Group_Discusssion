# Sends 3 minimal chat turns; prints prompt_eval_count and prompt_eval_cached_count from each done line.
param(
    [string]$Model = "eagle-clm",
    [string]$BaseUrl = "http://127.0.0.1:11434"
)

$ErrorActionPreference = "Stop"
$useThinkFalse = $Model -match "eagle-clm|qwen3"
$messages = New-Object System.Collections.Generic.List[hashtable]
$tmp = Join-Path $env:TEMP "eaglegd_bench_chat.json"

function Invoke-Turn([string]$userText) {
    $messages.Add(@{ role = "user"; content = $userText })
    $bodyObj = [ordered]@{
        model    = $Model
        messages = $messages.ToArray()
        stream   = $false
        options  = @{ num_ctx = 4096 }
    }
    if ($useThinkFalse) { $bodyObj.think = $false }
    $jsonBody = ($bodyObj | ConvertTo-Json -Depth 8 -Compress)
    [System.IO.File]::WriteAllText($tmp, $jsonBody, [System.Text.UTF8Encoding]::new($false))
    $raw = curl.exe -s "$BaseUrl/api/chat" -H "Content-Type: application/json" --data-binary "@$tmp"
    if (-not $raw) { throw "Empty response from Ollama for $Model" }
    $resp = $raw | ConvertFrom-Json
    if ($resp.error) { throw $resp.error }
    $messages.Add(@{ role = "assistant"; content = $resp.message.content })
    $cached = 0
    if ($null -ne $resp.PSObject.Properties['prompt_eval_cached_count']) {
        $cached = [int]$resp.prompt_eval_cached_count
    }
    [PSCustomObject]@{
        Model                    = $Model
        Turn                     = ($messages.Count / 2)
        prompt_eval_count        = [int]$resp.prompt_eval_count
        prompt_eval_cached_count = $cached
        prompt_eval_new          = [int]$resp.prompt_eval_count - $cached
        eval_count               = [int]$resp.eval_count
    }
}

$results = @()
foreach ($n in 1..3) {
    $results += Invoke-Turn(("Doubt turn {0}: short question about photosynthesis?" -f $n))
}
$results | Format-Table -AutoSize
