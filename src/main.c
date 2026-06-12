#include <genesis.h>

int main(bool hardReset)
{
    (void) hardReset;

    VDP_drawText("SPACE HARRIER STYLE PROTOTYPE", 5, 10);
    VDP_drawText("SKELETON BUILD OK", 11, 14);

    while (TRUE)
    {
        SYS_doVBlankProcess();
    }

    return 0;
}
