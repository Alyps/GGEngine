Option Explicit

Dim files, shell, root, repo, game, resources, command, logPath, result
Set files = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
root = files.GetParentFolderName(WScript.ScriptFullName)
repo = files.GetParentFolderName(root)
game = files.BuildPath(repo, "out\build\x64-qa-debug\Game\Game.exe")
resources = files.BuildPath(repo, "Engine\StandartResources")
logPath = files.BuildPath(root, "game.log")

If Not files.FileExists(game) Or Not files.FolderExists(resources) Then
    MsgBox "Build Game from the QA branch as described in CubeRelay\README.md.", 16, "Cube Relay"
    WScript.Quit 1
End If

' Hide only the launch shell. Game.exe creates its own normal game window.
command = Q(shell.ExpandEnvironmentStrings("%ComSpec%")) & " /d /s /c " & _
    Q(Q(game) & " --project " & Q(root) & " --scene res://Scenes/Main.level.json" & _
      " --resources res --standard-resources " & Q(resources) & " >" & Q(logPath) & " 2>&1")
shell.CurrentDirectory = root
result = shell.Run(command, 0, True)
If result <> 0 Then MsgBox "The game exited with code " & result & ". See game.log.", 16, "Cube Relay"
WScript.Quit result

Function Q(value)
    Q = Chr(34) & value & Chr(34)
End Function
