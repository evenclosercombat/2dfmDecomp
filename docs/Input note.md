Byte 1: 
Right digit: Arrow - 0 for 'free', 1 for dot, 09 for up right, 0A-0D for multi-arrows starting at left.   
Left-digit: Buttons A through D are stored in the left digit, bitmask-style. 

Byte 2: 
Left-digit -  Continue to next input = +0x30 End at this input = +0x20 No repeat/hold/black button = +0x00 Repeat = +0x40 Hold = +0x80 Black = +0xC0  
Right-digit: Buttons E (binary 1) and F (binary 2)

/* |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0  |
   |     |     |     |     |     |     |     |     |     |     |
   |  F  |  E  |  D  |  C  |  B  |  A  | DWN | JMP | BCK | FWD | */