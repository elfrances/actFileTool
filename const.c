/*=========================================================================
 *
 *   Filename:           const.c
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

// This constant indicates a nil/unspec elevation value
const double nilElev = -9999.99; // 10,000 meters below sea level! :)

// This constant indicates a nil/unspec grade/slope value
const double nilGrade = -99.99;  // free falling! :)

// This constant indicates a nil/unspec speed value
const double nilSpeed = 9999.99;    // Flashman! :)

const double degToRad = (double) 0.01745329252;  // decimal degrees to radians
const double earthMeanRadius = (double) 6372797.560856;  // in meters

// Banner line used in CSV files
const char *csvBannerLine = "<trkpt>,<inFile>,<line#>,<time>,<lat>,<lon>,<ele>,<power>,<atemp>,<cadence>,<hr>,<run>,<rise>,<dist>,<distance>,<speed>,<grade>,<deltaG>,<deltaS>,<deltaT>";


