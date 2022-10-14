/*
 * string_user.c
 *
 *  Created on: Feb 12, 2022
 *      Author: HAOHV6
 */
#include "../Mylib/string_user.h"

void String_process_backup_message(char* input, long t_on)
{
	char time_string_wait_fix[15] = {0};
	char time_string_fixed[15] = {0};
	char mess_fix_time[500] = {0};
	for(int i = 0; i < strlen(input); i++)
	{
		if(input[i] == 'T' && input[i-1] == '"' && input[i+1] == '"' && input[i+2] == ':')
		{
			int index = 0;
			for(int j =i+3; j < i+3+15; j++)
			{
				if(input[j] == ',')
				{
					break;
				}
				time_string_wait_fix[index++] = input[j];
			}
			break;
		}
	}
	long time_number = atol(time_string_wait_fix) + t_on;
	sprintf(time_string_fixed, "%ld", time_number);
	replace_sub_string(input, time_string_wait_fix, time_string_fixed,  mess_fix_time);
	memset(input, 0, strlen(input));
	strcpy(input, mess_fix_time);
}
void replace_sub_string(char* str, char* substr, char* replace, char* output)
{
     int i = 0, j = 0, flag = 0, start = 0;
     for(int i = 0; i < strlen(str); i ++)
     {
         if(str[i] == 'T' && str[i-1] == '"' && str[i+1] == '"' && str[i+2] == ':')
         {
             start = i+3;
             for(int j = i+3; j < strlen(str); j++)
             {
                if(strcmp(str + start, substr))
                {
                    flag = true;
                }
             }
             break;
         }
     }


    if (flag)
    {
            for (i = 0; i < start; i++)
                    output[i] = str[i];
            // replace substring with another string
            for (j = 0; j < strlen(replace); j++)
            {
                    output[i] = replace[j];
                    i++;
            }

            // copy remaining portion of the input string "str"
            for (j = start + strlen(substr); j < strlen(str); j++)
            {
                    output[i] = str[j];
                    i++;
            }

            // print the final string
            output[i] = '\0';
            //printf("Output: %s\n", output);
    } else {
            //printf("%s is not a substring of %s\n", substr, str);
    }
        return;
}
