<#
  シェーダー修正パッチ適用スクリプト

  使い方:
    1. shader-fix.patch と このスクリプトを同じフォルダに置く
    2. リポジトリのルート(zed.sln がある場所)で PowerShell を開く
    3. > .\apply-shader-fix.ps1 -PatchPath .\shader-fix.patch

  オプション:
    -Branch <名前>   作成/切り替えするブランチ名
                     (既定: claude/shader-demo-optimization-i0194n)
    -NoCommit        コミットせず作業ツリーへの変更適用だけ行う
                     (自分でコミットメッセージを書きたい場合)

  注意: shader-fix.patch はテキストエディタで開いて保存しないでください。
        HLSL は Shift-JIS、コミットメッセージは UTF-8 の混在バイト列のため
        再保存すると壊れます。
#>

[CmdletBinding()]
param(
    [string]$PatchPath = ".\shader-fix.patch",
    [string]$Branch    = "claude/shader-demo-optimization-i0194n",
    [switch]$NoCommit
)

$ErrorActionPreference = "Stop"

# パッチが想定通りの内容か（転送中に壊れていないか）確認するための値
$ExpectedSha256 = "601C7E9D2BA4DA3E56B03082B30FCC6CBA18143FE06F2B547AD88E2A147D70F6"
# このパッチが前提とする master のコミット
$BaseCommit = "68865a93c407726362f093e189a28cd75f8a91ac"

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

# --- 1. 事前チェック ---------------------------------------------------------

if (-not (Test-Path $PatchPath)) {
    Fail "パッチが見つかりません: $PatchPath"
}

git rev-parse --is-inside-work-tree *> $null
if ($LASTEXITCODE -ne 0) {
    Fail "git リポジトリの中で実行してください。"
}

# リポジトリのルートへ移動（サブフォルダから実行されても動くように）
$repoRoot = (git rev-parse --show-toplevel).Trim()
$PatchPath = (Resolve-Path $PatchPath).Path
Set-Location $repoRoot
Write-Host "リポジトリ: $repoRoot"

# パッチの整合性チェック
$actualSha = (Get-FileHash -Algorithm SHA256 -Path $PatchPath).Hash
if ($actualSha -ne $ExpectedSha256) {
    Write-Host "WARNING: パッチのハッシュが想定と異なります。" -ForegroundColor Yellow
    Write-Host "  期待値: $ExpectedSha256"
    Write-Host "  実際  : $actualSha"
    Write-Host "  ダウンロード時に改行コードや文字コードが変換された可能性があります。"
    $answer = Read-Host "  このまま続行しますか? (y/N)"
    if ($answer -ne "y") { exit 1 }
} else {
    Write-Host "パッチの整合性チェック: OK" -ForegroundColor Green
}

# 未コミットの変更があると適用に失敗するので止める
$dirty = git status --porcelain
if ($dirty) {
    Write-Host "未コミットの変更があります:" -ForegroundColor Yellow
    git status --short
    Fail "先に commit か stash をしてください。"
}

# ベースコミットの存在確認（無くても --3way で通ることがあるので警告のみ）
git cat-file -e "$BaseCommit^{commit}" *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host "WARNING: 前提コミット $($BaseCommit.Substring(0,7)) がローカルにありません。" -ForegroundColor Yellow
    Write-Host "         3-way マージでの適用を試みます。"
}

# --- 2. ブランチ準備 ---------------------------------------------------------

$currentBranch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($currentBranch -ne $Branch) {
    git show-ref --verify --quiet "refs/heads/$Branch"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "既存ブランチへ切り替え: $Branch"
        git checkout $Branch
    } else {
        Write-Host "ブランチを作成: $Branch"
        git checkout -b $Branch
    }
    if ($LASTEXITCODE -ne 0) { Fail "ブランチの切り替えに失敗しました。" }
} else {
    Write-Host "現在のブランチ: $Branch"
}

# --- 3. パッチ適用 -----------------------------------------------------------
# Windows の作業ツリーは .gitattributes の text=auto により CRLF に変換されている。
# --3way は blob 単位でマージするため、改行コードの差で失敗しない。

if ($NoCommit) {
    Write-Host "パッチを適用中 (コミットなし)..."
    git apply --3way --verbose $PatchPath
    if ($LASTEXITCODE -ne 0) { Fail "git apply に失敗しました。" }

    Write-Host ""
    Write-Host "適用しました (未コミット)。" -ForegroundColor Green
    git status --short
} else {
    Write-Host "パッチを適用中 (git am --3way)..."
    git am --3way $PatchPath
    if ($LASTEXITCODE -ne 0) {
        Write-Host "git am に失敗しました。作業を巻き戻します。" -ForegroundColor Yellow
        git am --abort *> $null
        Write-Host "git apply --3way にフォールバックします..."
        git apply --3way --verbose $PatchPath
        if ($LASTEXITCODE -ne 0) { Fail "パッチを適用できませんでした。" }
        Write-Host "作業ツリーへの適用のみ成功しました。commit は手動で行ってください。" -ForegroundColor Yellow
        git status --short
        exit 0
    }

    Write-Host ""
    Write-Host "適用しました。" -ForegroundColor Green
    git --no-pager log --oneline -1
    git --no-pager show --stat --oneline HEAD | Select-Object -Skip 1
}

Write-Host ""
Write-Host "次の手順:" -ForegroundColor Cyan
Write-Host "  1. Visual Studio でリビルドしてください。"
Write-Host "     Shader\*.cso は古いままなので、HLSL の再コンパイルが必要です。"
Write-Host "  2. 問題なければ push:"
Write-Host "     git push -u origin $Branch"
