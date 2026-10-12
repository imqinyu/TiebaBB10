param([switch]$IncludeUid, [switch]$SingleRequest, [switch]$Login)
$ErrorActionPreference = 'Stop'
# The credential is read from stdin, never embedded in this file or output.
$probeCredential = [Console]::In.ReadLine()
if ([string]::IsNullOrWhiteSpace($probeCredential)) { throw 'Credential input is required' }
$probeCredential = $probeCredential.Trim()
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$probeUid = ''
function Invoke-ForumProbe([string]$path, [hashtable]$extra, [string]$label) {
    $params = @{ BDUSS=$probeCredential; _client_type='20'; subapp_type='pc' }
    if ($Login) {
        $params = @{ bdusstoken=($probeCredential + '|'); _client_type='2'; _client_version='11.10.8.6'; _client_id=('wappc_' + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() + '_123'); _phone_imei='0000000000000000'; model='BlackBerry'; net_type='1'; timestamp=[string][DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); authsid='null'; channel_id=''; channel_uid=''; stoken='' }
    }
    foreach ($key in $extra.Keys) { $params[$key] = [string]$extra[$key] }
    [string[]]$keys = $params.Keys
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    $signText = ($keys | ForEach-Object { $_ + '=' + $params[$_] }) -join ''
    $salt = if ($Login) { 'tiebaclient!!!' } else { '36770b1f34c9bbf2e7d1a99d2b82fa9e' }
    $hash = [Security.Cryptography.MD5]::Create()
    try { $params['sign'] = [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($signText + $salt))).Replace('-','').ToUpperInvariant() }
    finally { $hash.Dispose() }
    $form = ($params.Keys | ForEach-Object { [Uri]::EscapeDataString($_) + '=' + [Uri]::EscapeDataString($params[$_]) }) -join '&'
    $request = [Net.HttpWebRequest]::Create('https://c.tieba.baidu.com' + $path)
    $request.Method = 'POST'
    $request.AllowAutoRedirect = $false
    $request.Timeout = 12000
    $request.ReadWriteTimeout = 12000
    $request.ContentType = 'application/x-www-form-urlencoded'
    $request.UserAgent = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
    $request.Accept = 'application/json, text/plain, */*'
    $request.Referer = 'https://tieba.baidu.com/'
    $request.Headers['Cookie'] = 'ka=open; BDUSS=' + $probeCredential
    if ($Login) { $request.UserAgent = 'bdtb for Android 11.10.8.6'; $request.Headers['Cookie'] = 'ka=open' }
    $bytes = [Text.Encoding]::UTF8.GetBytes($form)
    $request.ContentLength = $bytes.Length
    try {
        $stream = $request.GetRequestStream()
        try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
        $response = $request.GetResponse()
        try {
            $reader = New-Object IO.StreamReader($response.GetResponseStream(), [Text.Encoding]::UTF8)
            try { $raw = $reader.ReadToEnd() } finally { $reader.Dispose() }
            $json = $raw | ConvertFrom-Json
            $forumCount = 0
            if ($json.forum_list) {
                foreach ($bucket in $json.forum_list.PSObject.Properties) {
                    if ($bucket.Value -is [array]) { $forumCount += $bucket.Value.Count }
                }
            }
            if ($json.user.id) { $script:probeUid = [string]$json.user.id }
            $message = [string]$json.error_msg
            if (-not $message) { $message = [string]$json.error_message }
            $summary = [ordered]@{ probe=$label; scheme='https'; status=[int]$response.StatusCode; code=$json.error_code; message=$message.Replace($probeCredential,'[redacted]'); keys=@($json.PSObject.Properties.Name); groupedForums=$forumCount; recommendedForums=$(if ($json.like_forum) { @($json.like_forum).Count } else { 0 }); hasMore=$json.has_more; hasUser=!!$json.user.id }
            $summary | ConvertTo-Json -Compress -Depth 3
        } finally { $response.Dispose() }
    } catch {
        $kind = $_.Exception.GetType().Name
        $status = if ($_.Exception.InnerException -is [Net.WebException]) { [string]$_.Exception.InnerException.Status } else { 'request_failed' }
        @{ probe=$label; exception=$kind; status=$status } | ConvertTo-Json -Compress
    }
}
if ($Login) { Invoke-ForumProbe '/c/s/login' @{} 'dedicated-login'; $probeCredential=$null; exit 0 }
Invoke-ForumProbe '/c/f/forum/like' @{ page_no='1'; page_size='200' } 'current-paged-request'
if ($SingleRequest) { $probeCredential = $null; exit 0 }
Invoke-ForumProbe '/c/f/forum/forumrecommend' @{ like_forum='1'; recommend='0'; topic='0' } 'recommend-request'
if ($IncludeUid) {
    Invoke-ForumProbe '/c/f/frs/page_pc' @{ kw='BlackBerry'; pn='1'; rn='1'; is_newfrs='1'; is_newfeed='1' } 'profile-probe'
    if ($probeUid) { if ($Login) { Invoke-ForumProbe '/c/s/login' @{} 'dedicated-login'; $probeCredential=$null; exit 0 }
Invoke-ForumProbe '/c/f/forum/like' @{ page_no='1'; page_size='200'; uid=$probeUid } 'paged-request-with-uid' }
}
$probeCredential = $null
