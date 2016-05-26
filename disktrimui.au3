#NoTrayIcon
#RequireAdmin
#include <GUIConstantsEx.au3>
#include <WindowsConstants.au3>
#include <ComboConstants.au3>
#include <StaticConstants.au3>
#include <ButtonConstants.au3>
#include <AutoItConstants.au3>
#include <Array.au3>
#AutoIt3Wrapper_icon=disk.ico
#AutoIt3Wrapper_Res_Description=DiskTrim GUI Wrapper
#AutoIt3Wrapper_Res_LegalCopyright=(c) 2016 by Antoni Sawicki
#AutoIt3Wrapper_Res_ProductVersion=1.2.0.0
#AutoIt3Wrapper_Res_Fileversion=1.2.0.0
#AutoIt3Wrapper_Res_RequestedExecutionLevel=RequireAdministrator

FileInstall("disktrim-x64.exe", @TempDir & "\disktrim.exe")


Global $Command = ""
Global $Combos = ""
Global $Disk[0][2]

$cimv2 = ObjGet('winmgmts:root\cimv2')
$Col=$cimv2.ExecQuery('Select * from Win32_DiskDrive Where MediaType like "Fixed hard disk media"')

For $item In $Col
   _ArrayAdd($Disk, $item.name & "|Disk " & $item.index & " : " & $item.caption & " : " & round($item.size/1024/1024/1024) & " GB")
Next

For $i = 0 To UBound($Disk) -1
    $Combos &= "|" & $Disk[$i][1]
Next

$hGUI = GUICreate("DiskTrim v1.2 - Erases entire SSD by TRIM / UNMAP", 500, 240)
$hOpDrop = GUICtrlCreateCombo("", 10, 10, 300, 20, $CBS_DROPDOWNLIST)
$hChkBox = GUICtrlCreateCheckbox("Yes, wipe clean all data from selected disk", 10, 40)
$hOut = GUICtrlCreateLabel("", 10, 70, 480, 160, $SS_SUNKEN)
$hSend = GUICtrlCreateButton("TRIM", 320, 10, 80, 50)
$hExit = GUICtrlCreateButton("EXIT", 410, 10, 80, 50, $BS_DEFPUSHBUTTON)

GUICtrlSetData($hOpDrop, $combos)

GUISetState()
While 1
    Switch GUIGetMsg()
        Case $GUI_EVENT_CLOSE, $hExit
            Exit
        Case $hOpDrop
            $iIndex = _ArraySearch($Disk, GUICtrlRead($hOpDrop))
            If Not @error Then
				$Command=@TempDir & "\" &"disktrim.exe -y " & $Disk[$iIndex][0]
				GUICtrlSetData($hOut, "WARNING:" & @CRLF & "ALL DATA ON " & $Disk[$iIndex][1] & " WILL BE ERASED!")
			EndIf
		Case $hSend
			if stringlen($Command) > 0 and BitAND(GUICtrlRead($hChkBox), $GUI_CHECKED) = $GUI_CHECKED then
			   $PID=Run($Command, "", @SW_HIDE, $STDOUT_CHILD)
			   ProcessWaitClose($PID)
			   GUICtrlSetData($hOut, StdoutRead($PID))
			endIf

    EndSwitch
WEnd