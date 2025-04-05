#pragma once

//translate extended hex values to correct extended ascii values
//Swedish user provided examples of characters not displaying correctly

extern inline char shift_extended_char(char extended_char)  //using known values provided in https://github.com/pletch/esphome-vistaECP-idf/issues/3#issuecomment-2780955362
{
    switch (extended_char)
    {
        case 0xE1:
            return 0xE4;
        case 0xEF:
            return 0xF6;
        default:
            return extended_char;
    }
}

