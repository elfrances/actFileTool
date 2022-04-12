/*=========================================================================
 *
 *   Filename:           input.h
 *
 *   Author:             Marcelo Mourier
 *   Created:            Mon Apr 11 11:24:16 MDT 2022
 *
 *   Description:        Input file parsers
 *
 *=========================================================================
 *
 *                  Copyright (c) 2022 Marcelo Mourier
 *
 *=========================================================================
*/

#ifndef INPUT_H_
#define INPUT_H_

#include "defs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int parseFitFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile);
extern int parseGpxFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile);
extern int parseTcxFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile);

#ifdef __cplusplus
};
#endif

#endif /* INPUT_H_ */
