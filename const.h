/*=========================================================================
 *
 *   Filename:           const.h
 *
 *   Author:             Marcelo Mourier
 *   Created:            Mon Apr 11 11:24:16 MDT 2022
 *
 *   Description:        Miscellaneous constants
 *
 *=========================================================================
 *
 *                  Copyright (c) 2022 Marcelo Mourier
 *
 *=========================================================================
*/

#ifndef CONST_H_
#define CONST_H_

extern const double nilElev;            // nil/undefined elevation value
extern const double nilGrade;           // nil/undefined grade/slope value
extern const double nilSpeed;           // nil/undefined speed value

extern const double degToRad;           // decimal degrees to radians
extern const double earthMeanRadius;    // Mean radius of Earth, in meters

// Banner line used in CSV files
extern const char *csvBannerLine;

#endif /* CONST_H_ */
