0 REM From AUG; altered for 75 baud
1 :
10 REM Fudge factor: put 2 dummy bytes in RS423 input buffer 
20 REM to allow control of RTS flag by use of buffer
30 REM tolerance (OSBYTE &CB, 203)
40 *FX 138,1,1
50 *FX 138,1,1
60 REM Enable receive interrupts to allow control of RTS
70 *FX 2,2
80 REM Indicate that RS423 is cassette
90 *FX 205,64
100 REM Select baud rates
105 REM !! 'Diminished': now pick 75 baud in Serial ULA 
110 *FX 7,1
120 *FX 8,1
130 REM Reset 6850
140 *FX 156,3,252
150 *FX 156,2,252
160 REM Turn tone on
170 *FX 203,9
180 REM Turn motor on
190 *MOTOR 1
200 REM Inform user
210 PRINT "Press record and return"
220 DUMMY=GET
230 REM Select output route
239 REM !! Diminished: don't print to screen
240 *FX 3,3
250 REM Send ULA synchronisation
260 VDU &AA
270 REM Wait (header tone)
280 TIME=0
290 REPEAT UNTIL TIME=500
300 REM !! 'Diminished': payload change 
310 FOR X=0 TO 255:VDU X:NEXT
320 REM Wait until buffer empty
330 REPEAT UNTIL ADVAL(-3)>&BE
340 REM Pause for a short period of tone
350 TIME=0
360 REPEAT UNTIL TIME=50
370 REM Disconnect RS423 output
380 *FX 3,0
390 REM Turn off tone
400 *FX 203,255
410 REM Wait (for an interblock gap of silence)
420 TIME=0
430 REPEAT UNTIL TIME=150
440 REM Turn motor off
450 *MOTOR 0
460 REM Restore RS423
470 *FX 205,0
480 REM Tidy up serial input
490 *FX 2,0
500 *FX 21,1



