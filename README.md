Emulator of i8080 processor and personal computers, based on it.

Based on 4004 emulator. Now under development.


Keys for command string:
8080_emulator.exe -f BusiCom.txt -ru -list -step -log
 -f <filename>   - txt file with program
 -ru             - russian localization
 -list           - list program before run
 -step           - step by step execution (<Space> to run next command, press <P> to disable/enable it, <TAB> to list all registers)
 -log            - log commands to console


Program file format : <command in decimal> # <comment>
Example:
>1  64 # jump to start
>2  0
>3  209 # ld 1


