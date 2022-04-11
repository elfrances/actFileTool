/*=========================================================================
 *
 *   Filename:           main.c
 *
 *   Author:             Marcelo Mourier
 *   Created:            Mon Apr 11 11:24:16 MDT 2022
 *
 *   Description:        Type definitions
 *
 *=========================================================================
 *
 *                  Copyright (c) 2022 Marcelo Mourier
 *
 *=========================================================================
*/

#ifndef DEFS_H_
#define DEFS_H_

#include <stdio.h>

#include "tailq.h"

// Program version info
#define PROG_VER_MAJOR  1
#define PROG_VER_MINOR  6

typedef enum Bool {
    false = 0,
    true = 1
} Bool;

// Sensor data bit masks
#define SD_NONE     0x00    // no metrics
#define SD_ATEMP    0x01    // ambient temperature
#define SD_CADENCE  0x02    // cadence
#define SD_HR       0x04    // heart rate
#define SD_POWER    0x08    // power
#define SD_ALL      0x0f    // all metrics

// GPS Track Point
typedef struct TrkPt {
    TAILQ_ENTRY(TrkPt)   tqEntry;   // node in the trkPtList

    int index;          // TrkPt index (0..N-1)

    int lineNum;        // line number in the input GPX/TCX file
    const char *inFile; // input GPX/TCX file this trkpt came from

    // Timestamp from GPX/TCX file
    double timestamp;   // in seconds+millisec since the Epoch

    // GPS data from GPX/TCX file
    double latitude;    // in degrees decimal
    double longitude;   // in degrees decimal
    double elevation;   // in meters

    // Extra data from GPX/TCX file
    int ambTemp;        // ambient temperature (in degrees Celsius)
    int cadence;        // pedaling cadence (in RPM)
    int heartRate;      // heart rate (in BPM)
    int power;          // pedaling power (in watts)
    double speed;       // speed (in m/s)
    double distance;    // distance from start (in meters)

    // Computed metrics
    Bool adjGrade;      // grade was adjusted
    double adjTime;     // adjusted timestamp
    double deltaT;      // time diff with previous point (in seconds)
    double dist;        // distance traveled from previous point (in meters)
    double rise;        // elevation diff from previous point (in meters)
    double run;         // horizontal distance from previous point (in meters)

    double bearing;     // initial bearing / forward azimuth (in decimal degrees)
    double grade;       // actual grade (in percentage)
} TrkPt;

// GPS Track (sequence of Track Points)
typedef struct GpsTrk {
    // List of TrkPt's
    TAILQ_HEAD(TrkPtList, TrkPt) trkPtList;

    // Number of TrkPt's in trkPtList
    int numTrkPts;

    // Number of TrkPt's that had their elevation values
    // adjusted to match the min/max grade levels.
    int numElevAdj;

    // Number of TrkPt's discarded because they were a
    // duplicate of the previous point.
    int numDupTrkPts;

    // Number of TrkPt's trimmed out (by user request)
    int numTrimTrkPts;

    // Number of dummy TrkPt's discarded; e.g. because
    // of a null deltaT or a null deltaD.
    int numDiscTrkPts;

    // Activity type
    int type;

    // Bitmask of optional metrics present in the input
    int inMask;

    // Activity's start/end times
    double startTime;
    double endTime;

    // Base time
    double baseTime;                // time reference to generate relative timestamps

    // Time offset
    double timeOffset;              // to set/change the activity's start time

    // Aggregate values
    int heartRate;
    int cadence;
    int power;
    int temp;
    double time;
    double stoppedTime;             // amount of time with speed=0
    double distance;
    double elevGain;
    double elevLoss;
    double grade;

    // Max values
    int maxCadence;
    int maxHeartRate;
    int maxPower;
    int maxTemp;
    double maxDeltaD;
    double maxDeltaG;
    double maxDeltaT;
    double maxElev;
    double maxGrade;
    double maxSpeed;

    // Min values
    int minCadence;
    int minHeartRate;
    int minPower;
    int minTemp;
    double minElev;
    double minGrade;
    double minSpeed;

    const TrkPt *maxCadenceTrkPt;   // TrkPt with max cadence value
    const TrkPt *maxDeltaDTrkPt;    // TrkPt with max dist diff
    const TrkPt *maxDeltaGTrkPt;    // TrkPt with max grade diff
    const TrkPt *maxDeltaTTrkPt;    // TrkPt with max time diff
    const TrkPt *maxElevTrkPt;      // TrkPt with max elevation value
    const TrkPt *maxGradeTrkPt;     // TrkPt with max grade value
    const TrkPt *maxHeartRateTrkPt; // TrkPt with max HR value
    const TrkPt *maxPowerTrkPt;     // TrkPt with max power value
    const TrkPt *maxSpeedTrkPt;     // TrkPt with max speed value
    const TrkPt *maxTempTrkPt;      // TrkPt with max temp value

    const TrkPt *minCadenceTrkPt;   // TrkPt with min cadence value
    const TrkPt *minDeltaDTrkPt;    // TrkPt with min dist diff
    const TrkPt *minDeltaTTrkPt;    // TrkPt with min time diff
    const TrkPt *minElevTrkPt;      // TrkPt with max elevation value
    const TrkPt *minGradeTrkPt;     // TrkPt with min grade value
    const TrkPt *minHeartRateTrkPt; // TrkPt with min HR value
    const TrkPt *minPowerTrkPt;     // TrkPt with min power value
    const TrkPt *minSpeedTrkPt;     // TrkPt with min speed value
    const TrkPt *minTempTrkPt;      // TrkPt with min temp value
} GpsTrk;

// Activity type
typedef enum ActType {
    undef = 0,
    ride = 1,
    hike = 4,
    run = 9,
    walk = 10,
    vride = 17,
    other = 99
} ActType;

// Output file format
typedef enum OutFmt {
    nil = 0,
    csv = 1,    // Comma-Separated-Values format
    gpx = 2,    // GPS Exchange format
    shiz = 3,   // FulGaz format
    tcx = 4     // Training Center Exchange format
} OutFmt;

// Timestamp format
typedef enum TsFmt {
    none = 0,
    sec = 1,    // plain seconds
    hms = 2     // hh:mm:ss
} TsFmt;

// Moving Average method
typedef enum XmaMethod {
    simple = 1,         // SMA
    weighed = 2         // WMA
} XmaMethod;

// Metric used for the SMA/WMA
typedef enum XmaMetric {
    elevation = 1,      // elevation
    grade = 2,          // grade
    power = 3           // power
} XmaMetric;

typedef struct CmdArgs {
    int argc;               // number of arguments
    char **argv;            // list of arguments
    const char *inFile;     // input file name

    ActType actType;        // activity type for the output file
    int closeGap;           // close the time gap at the specified track point
    double maxGrade;        // max grade allowed (in %)
    double maxGradeChange;  // max grade change allowed between points (in %)
    double minGrade;        // min grade allowed (in %)
    const char *name;       // <name> tag
    FILE *outFile;          // output file
    OutFmt outFmt;          // format of the output data (csv, gpx)
    int outMask;            // bitmask of optional metrics to be included in the output
    Bool quiet;             // don't print any warning messages
    int rangeFrom;          // start point (inclusive)
    int rangeTo;            // end point (inclusive)
    TsFmt relTime;          // show relative timestamps in the specified format
    double setSpeed;        // speed to use to generate timestamps (in m/s)
    XmaMethod xmaMethod;    // method to compute the Moving Average
    XmaMetric xmaMetric;    // metric to use for the SMA/WMA
    int xmaWindow;          // size of the SMA/WMA window
    double startTime;       // start time for the activity
    Bool summary;           // show data summary
    Bool trim;              // trim points
    Bool verbatim;          // no data adjustments
} CmdArgs;

#ifdef __cplusplus
extern "C" {
#endif

static __inline__ double mToKm(double m) { return (m / 1000.0); }
static __inline__ double mpsToKph(double mps) { return (mps * 3.6); }

#ifdef __cplusplus
};
#endif

#endif /* DEFS_H_ */
