/*---------------------------------------------------------------------------------

	Simple console print demo
	-- dovoto

---------------------------------------------------------------------------------*/
#include <nds.h>
#include <stdio.h>

//---------------------------------------------------------------------------------
int main(void) {
//---------------------------------------------------------------------------------
	consoleDemoInit();  //setup the sub screen for printing

	iprintf("  DS WIFI TEST\n");
    nocashMessage("hello\n");

    char msg[129];
    
	while(1) {

		//touchRead(&touch);
		//iprintf("\x1b[2;0HTouch x = %04i, %04i\n", touch.rawx, touch.px);
        
        //if (fifoCheckDatamsg(FIFO_USER_01))
        //    fifoGetDatamsg(FIFO_USER_01, 8, (u8*)&buf);
        
        /*
        while (fifoCheckValue32(FIFO_USER_01)) {
            iprintf("\x1B[14;0H## %08lx ", fifoGetValue32(FIFO_USER_01));
        }
            
        while (fifoCheckValue32(FIFO_USER_02)) {
            void *addr = (void *)fifoGetValue32(FIFO_USER_02);
            iprintf("\x1B[%u;0H- %p", pos++, addr);
        }
        
        while (fifoCheckDatamsg(FIFO_USER_02)) {
            int length = fifoGetDatamsg(FIFO_USER_02, 32, ssid);
            ssid[length] = 0;
            
            //u32 value = fifoGetValue32(FIFO_USER_02);
            iprintf("\x1B[%u;0H> %s", pos++, ssid);
        }*/
        
        while (fifoCheckDatamsg(FIFO_USER_03)) {
            int length = fifoGetDatamsg(FIFO_USER_03, 129, msg);
            msg[length] = 0;
            iprintf("%s", msg);
        }

		swiWaitForVBlank();
		scanKeys();
		if (keysDown()&KEY_START) break;
	}

	return 0;
}
