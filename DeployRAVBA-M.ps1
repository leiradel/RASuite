﻿Function DeployVBA
{
    Param(
        [Parameter(Mandatory)]
        [ValidateSet("True","False")]
        $ForceUpdate
        )

    Process
    {
        ################################################################################
        # Custom Variables:

        $FilesToCopy = @(".\RA_Integration\Overlay",
                         ".\RAVBA-M\project\vs2012_mfc\Release\RAVisualBoyAdvance-M.exe",
                         ".\RAVBA-M\project\vs2012_mfc\Release\D3DX9_41.dll",
                         ".\RAVBA-M\Deploy\COPYING",
                         ".\RAVBA-M\Deploy\NEWS",
                         ".\RAVBA-M\Deploy\README-win.txt");

        $TargetArchiveName = "RAVBA.zip"

        $VersionDoc = "..\RAWeb\public\LatestRAVBAVersion.html"

        $ExpectedTag = "RAVBA-M"


        ################################################################################
        # Global Variables:

        $Password = ConvertTo-SecureString 'Unused' -AsPlainText -Force
        $Credential = New-Object System.Management.Automation.PSCredential ('ec2-user', $Password)
        $KeyPath = ".\RetroAchievementsKey"
        $TargetURL = "www.RetroAchievements.org"
        $WebRoot = "/var/www/html/public"
        $WebRootBin = "/var/www/html/public/bin"

        if (-not (Test-Path "$env:ProgramFiles\7-Zip\7z.exe")) 
        {
            throw "$env:ProgramFiles\7-Zip\7z.exe needed"
        }
        Set-Alias sz "$env:ProgramFiles\7-Zip\7z.exe"

        
        ################################################################################
        # Test we are ready for release
        $latestTag = git describe --tags --match "$($ExpectedTag).*"
        $diffs = git diff HEAD
        if( ![string]::IsNullOrEmpty( $diffs ) )
        {
            if( $ForceUpdate -ne "True" )
            {
                throw "Changes exist, cannot deploy!"
            }
            else
            {
                Write-Warning "Changes exist, deploying anyway!"
            }
        }

        $newHTMLContent = "0." + $latestTag.Substring( $ExpectedTag.Length + 1, 3 )
        $currentVersion = Get-Content $VersionDoc
        
        if( $newHTMLContent -eq $currentVersion )
        {
            if( $ForceUpdate -ne "True" )
            {
                throw "We are already on version $currentVersion, nothing to do!"
            }
            else
            {
                Write-Warning "We are already on version $currentVersion, deploying anyway!"
            }
        }

        Set-Content $VersionDoc $newHTMLContent


        ################################################################################
        # Produce Archive zip

        # Create stage
        if ( Test-Path ".\Stage" )
        {
            Remove-Item ".\Stage" -Force -Recurse
        }
        $stage = New-Item -ItemType Directory -Name "Stage" -Force

        # Copy all required to stage
        Foreach( $filepath in $FilesToCopy )
        {
            Copy-Item $filepath $stage.PSPath -Recurse -Force
        }

        sz a $TargetArchiveName "$($stage.FullName)\*.*" -r > 7z.log


        ################################################################################
        # Upload

        # Establish the SFTP connection
        $session = ( New-SFTPSession -ComputerName $TargetURL -Credential $Credential -KeyFile $KeyPath )

        # Upload the new version html
        Set-SFTPFile -SessionId $session.SessionId -LocalFile $VersionDoc -RemotePath $WebRoot -Overwrite

        # Upload the zip to the SFTP bin path
        Set-SFTPFile -SessionId $session.SessionId -LocalFile $TargetArchiveName -RemotePath $WebRootBin -Overwrite

        # Disconnect SFTP session
        $session.Disconnect()

        # Kill Stage
        Remove-Item ".\Stage" -Force -Recurse

        # Kill new zip
        Remove-Item $TargetArchiveName -Force
    }
}

################################################################################
# Set working directory:

$dir = Split-Path $MyInvocation.MyCommand.Path
Set-Location $dir

DeployVBA
