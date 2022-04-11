/*=========================================================================
 *
 *   Filename:           output.c
 *
 *   Author:             Marcelo Mourier
 *   Created:            Mon Apr 11 11:24:16 MDT 2022
 *
 *   Description:        Print the output data
 *
 *=========================================================================
 *
 *                  Copyright (c) 2022 Marcelo Mourier
 *
 *=========================================================================
*/

#ifndef OUTPUT_H_
#define OUTPUT_H_

#include "defs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void printOutput(GpsTrk *pTrk, CmdArgs *pArgs);

#ifdef __cplusplus
};
#endif

#endif /* OUTPUT_H_ */
