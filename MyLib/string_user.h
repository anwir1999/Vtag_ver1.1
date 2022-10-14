/*
 * string_user.h
 *
 *  Created on: Feb 12, 2022
 *      Author: HAOHV6
 */

#ifndef MYLIB_STRING_USER_H_
#define MYLIB_STRING_USER_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

void String_process_backup_message(char* input, long t_on);
void replace_sub_string(char* str, char* substr, char* replace, char* output);

#endif /* MYLIB_STRING_USER_H_ */
