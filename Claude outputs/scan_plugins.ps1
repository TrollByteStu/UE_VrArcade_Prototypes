$root = 'C:\Users\emil_\UE_VrArcade_Prototypes\Content'
$out  = Join-Path $env:TEMP 'monkey_plugin_scan.txt'
$patterns = @(
 '/Script/PaperZD','/PaperZD/','/Script/Mover','/MoverExamples/','/Script/Soundscape','/Script/GameplayStateTree',
 '/Script/PCGEx','/PCGExtendedToolkit/','/Script/AdvancedSessions','/Script/AdvancedSteamSessions',
 '/Script/HttpBlueprint','/Script/JsonBlueprintUtilities','/Script/PCGInstancedActorsInterop','/Script/ImpostorBaker',
 '/Script/OpenXRHandTracking','/Script/AIAssistant','/Script/AsyncLoadingScreen','/Script/OnlineSubsystemSteam',
 '/Script/OnlineSubsystemUtils','/Script/UsdStage','/Script/PCG"','/Script/PCG'
)
$enc = [System.Text.Encoding]::ASCII
$hits = @{}; foreach($p in $patterns){ $hits[$p] = New-Object System.Collections.Generic.List[string] }
$files = Get-ChildItem $root -Recurse -File -Include *.uasset,*.umap
$i=0
foreach($f in $files){
  $i++
  if($i % 1000 -eq 0){ "progress $i / $($files.Count)" | Set-Content "$out.progress" }
  try { $txt = $enc.GetString([System.IO.File]::ReadAllBytes($f.FullName)) } catch { continue }
  foreach($p in $patterns){ if($txt.IndexOf($p,[StringComparison]::Ordinal) -ge 0){ $hits[$p].Add($f.FullName.Substring($root.Length+1)) } }
}
$sb = New-Object System.Text.StringBuilder
foreach($p in $patterns){
  $l = $hits[$p]
  [void]$sb.AppendLine("=== $p : $($l.Count) files")
  $l | ForEach-Object { ($_ -split '\\')[0] } | Group-Object | Sort Count -Desc | ForEach-Object { [void]$sb.AppendLine("   $($_.Count)  $($_.Name)") }
  $l | Select-Object -First 6 | ForEach-Object { [void]$sb.AppendLine("      e.g. $_") }
}
$sb.ToString() | Set-Content $out
"DONE" | Set-Content "$out.progress"
