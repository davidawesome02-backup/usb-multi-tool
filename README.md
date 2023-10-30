# General purpose usb device by davidawesome
(Based on bouffalo sdk) Tested on bl808 in Ox64 (pine64)

________

## This project is not the best, so just report errors if you find them, I will try to fix
## Made with massive help from baselalsayeh



V1 - released oct 29 2023 (minor patches may not effect this file)

Patch notes:


UF2 flashing working <br>
iso 9660 and dd working <br>
write caching<br>
image resizing<br>
help files<br>
HID device faking working: <br>
| cmd | action | params | comments |
| -- | -- | -- | -- |
| hi | hid init | | |
| hd | hid disable | | |
| wu | wait usb connection | | |
| gs | general sleep       | \<ms\> | |
| gj | general jump        | \<delta_lines\> | |
| ls | led set             | \<red\> \<green\> \<blue\> | All are eithor 0 or 1, use 1 for on, 0 for off |
| ks | keyboard string     | \<modifiers\> \<resend_count\> \<string\> | |
| ki | keyboard input      | \<type\> \<resend_count\> \<modifiers\> [data]{x6} | Type is 0 for hid codes and 1 for ascii characters as data |
| mi | mouse input         | \<x\> \<y\> <scroll\> \<buttons\> | |
| cj | conditional jump    | \<delta_lines\> | |

## For more info, please read the help file located in examples directory