#include <windows.h>

#include "custom_helpers.h"

int main(void)
{
    if (vkToKeyId(VK_SPACE) != 4) return 1;
    if (vkToKeyId(VK_RETURN) != 6) return 2;
    if (vkToKeyId(VK_LEFT) != 1) return 3;
    if (vkToKeyId('A') != -1) return 4;
    if (vkToKeyId(VK_TAB) != -1) return 5;
    return 0;
}