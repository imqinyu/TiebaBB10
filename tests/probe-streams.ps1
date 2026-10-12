param([string]$Mode='all')
$ErrorActionPreference='Stop'
$credential=[Console]::In.ReadLine().Trim()
$script:firstIds=@{}
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
function Probe([string]$path,[string]$channel,[hashtable]$extra) {
 $params=if($channel -eq 'pc') { @{BDUSS=$credential;_client_type='20';subapp_type='pc'} } else { @{BDUSS=$credential;_client_type='2';_client_version='8.2.2';_client_id='wappc_stream_probe';cuid='00000000000000000000000000000000|0';from='baidu_appstore';net_type='1';timestamp=[string][DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()} }
 foreach($key in $extra.Keys){$params[$key]=[string]$extra[$key]}
 [string[]]$keys=$params.Keys;[Array]::Sort($keys,[StringComparer]::Ordinal)
 $salt=if($channel -eq 'pc'){'36770b1f34c9bbf2e7d1a99d2b82fa9e'}else{'tiebaclient!!!'}
 $hash=[Security.Cryptography.MD5]::Create();$raw=($keys|ForEach-Object{$_+'='+$params[$_]})-join ''
 $params.sign=[BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($raw+$salt))).Replace('-','');$hash.Dispose()
 $form=($params.Keys|ForEach-Object{[Uri]::EscapeDataString($_)+'='+[Uri]::EscapeDataString($params[$_])})-join '&'
 $req=[Net.HttpWebRequest]::Create('https://c.tieba.baidu.com'+$path);$req.Method='POST';$req.Timeout=10000;$req.ReadWriteTimeout=10000;$req.AllowAutoRedirect=$false;$req.ContentType='application/x-www-form-urlencoded';$req.UserAgent=if($channel -eq 'pc'){'Mozilla/5.0'}else{'bdtb for Android 8.2.2'};$req.Headers['Cookie']='ka=open; BDUSS='+$credential
 $bytes=[Text.Encoding]::UTF8.GetBytes($form);$req.ContentLength=$bytes.Length
 try {$stream=$req.GetRequestStream();$stream.Write($bytes,0,$bytes.Length);$stream.Dispose();$response=$req.GetResponse();$reader=New-Object IO.StreamReader($response.GetResponseStream(),[Text.Encoding]::UTF8);$data=$reader.ReadToEnd()|ConvertFrom-Json;$reader.Dispose();$summary=[ordered]@{path=$path;channel=$channel;http=[int]$response.StatusCode;code=$data.error_code;keys=@($data.PSObject.Properties.Name)};foreach($name in @('thread_list','feed','reply_list','at_list','data','hot_thread_list','thread_info','page')){if($null -ne $data.$name){$summary[$name+'Type']=$data.$name.GetType().Name;$summary[$name+'Count']=@($data.$name).Count;$first=@($data.$name)[0];$summary[$name+'Keys']=@($first.PSObject.Properties.Name)}};if($data.thread_info){$node=@($data.thread_info)[0];foreach($nested in @('thread_list','post_data','author','forum_info')){if($node.$nested){$summary[$nested+'NestedKeys']=@($node.$nested.PSObject.Properties.Name)}}};if($data.thread_list){$node=@($data.thread_list)[0];foreach($nested in @('author','forum_info','abstract')){if($node.$nested){$summary[$nested+'NestedKeys']=@(@($node.$nested)[0].PSObject.Properties.Name)}}};$summary['hasMore']=$data.has_more;$summary['pageHasMore']=$data.page.has_more;$summary['currentPage']=$data.page.current_page;$ids=@();if($data.thread_info){$ids=@($data.thread_info|ForEach-Object{if($_.thread_list){[string]$_.thread_list.id}else{[string]$_.id}})}elseif($data.thread_list){$ids=@($data.thread_list|ForEach-Object{[string]$_.id})};if($script:firstIds.ContainsKey($path)){$summary['overlapWithFirst']=@($ids|Where-Object{$script:firstIds[$path] -contains $_}).Count}else{$script:firstIds[$path]=$ids};$response.Dispose();$summary|ConvertTo-Json -Depth 4 -Compress;if($path -eq '/c/f/concern/userlike' -and !$extra.ContainsKey('page_tag')){Probe $path $channel @{pn='2';load_type='2';follow_type='1';page_tag=[string]$data.page_tag;last_request_unix=[string]$data.req_unix}}}
 catch {@{path=$path;channel=$channel;failed=$_.Exception.GetType().Name}|ConvertTo-Json -Compress}
}
foreach($channel in @('pc')) {
 Probe '/c/f/excellent/personalized' $channel @{pn='1';load_type='1';page_thread_count='15';need_tags='0';q_type='1';need_forumlist='0';subapp_type=$(if($channel -eq 'pc'){'pc'}else{'mini'})}
 Probe '/c/f/concern/userlike' $channel @{pn='1';load_type='1';follow_type='1'}
 Probe '/c/f/forum/hotThreadList' $channel @{tab_id='1';tab_code='all';pn='1'}
 Probe '/c/u/feed/replyme' $channel @{pn='0'}
 Probe '/c/u/feed/atme' $channel @{pn='0'}
}
$credential=$null