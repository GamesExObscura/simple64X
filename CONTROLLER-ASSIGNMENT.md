# Physical controller assignments

Open Input Configuration, choose a Player tab, and select the desired named profile before clicking Assign Controller. Turn on the controller before opening the assignment dialog, then press a button. Cancel or timeout leaves the assignment unchanged.

The existing SDL device-path matching remains unchanged. The selected identity is written to ControllerN/Gamepad in input-settings.ini. Named profiles additionally store ControllerN/Gamepad inside the profile; selecting that profile restores its preference for that port. Auto stores only the player assignment. Binding maps can still be shared between players without sharing physical controller identities.

Set Gamepad to None to unassign the port. Auto allows automatic assignment. Deleting a profile does not remove the port's existing assignment. A device already explicitly assigned to another port is rejected; unassign it from that port first. Existing Auto ports continue to use the existing runtime behavior.

When updating another PC, replace only simple64-input-qt.dll in its emulator folder (with simple64 closed). Keep that PC's existing INI files. Paths are local to the Windows installation; assign controllers again if moving settings between PCs or re-pairing changes the device paths.

Build with ./build-input-native.ps1; add -Check for isolated offscreen settings tests. The script uses C:/msys64/mingw64 and restores process PATH afterward; no user or system PATH modifications are made.

Verified: build, assignment persistence, named-profile restoration, dropdown-refresh preservation, legacy Path/Path64 duplicate recognition, None/Auto behavior, and other-port preservation. Physical 8BitDo button capture and power-cycle reconnection still require user testing. No live controller INI files were edited during development.
