/*=========================================================================
 *
 *   Filename:           main.c
 *
 *   Author:             Marcelo Mourier
 *   Created:            Fri Mar 12 09:56:32 PST 2021
 *
 *   Description:        This tool is used to process the activity metrics
 *                       in a GPX or TCX file. See the README.md file for
 *                       more details.
 *
 *=========================================================================
 *
 *                  Copyright (c) 2021 Marcelo Mourier
 *
 *=========================================================================
*/

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tailq.h"  // TAILQ macros

#ifdef _MSC_FULL_VER
// As usual, Windows/MSC has its own idiosyncrasies...
#include "win/gmtime_r.c"
#include "win/strptime.c"
#endif  // _MSC_FULL_VER

// Program version info
static const int progVerMajor = 1;
static const int progVerMinor = 3;

// Compile-time build info
static const char *buildInfo = "built on " __DATE__ " at " __TIME__;

// This constant indicates a nil elevation
static const double nilElev = -9999.99; // 10,000 meters below sea level! :)

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

    // Max values
    int maxCadence;
    int maxHeartRate;
    int maxPower;
    int maxTemp;
    double maxDeltaD;
    double maxDeltaG;
    double maxDeltaT;
    double maxGrade;
    double maxSpeed;

    // Min values
    int minCadence;
    int minHeartRate;
    int minPower;
    int minTemp;
    double minGrade;
    double minSpeed;

    const TrkPt *maxCadenceTrkPt;   // TrkPt with max cadence value
    const TrkPt *maxDeltaDTrkPt;    // TrkPt with max dist diff
    const TrkPt *maxDeltaGTrkPt;    // TrkPt with max grade diff
    const TrkPt *maxDeltaTTrkPt;    // TrkPt with max time diff
    const TrkPt *maxGradeTrkPt;     // TrkPt with max grade value
    const TrkPt *maxHeartRateTrkPt; // TrkPt with max HR value
    const TrkPt *maxPowerTrkPt;     // TrkPt with max power value
    const TrkPt *maxSpeedTrkPt;     // TrkPt with max speed value
    const TrkPt *maxTempTrkPt;      // TrkPt with max temp value

    const TrkPt *minCadenceTrkPt;   // TrkPt with min cadence value
    const TrkPt *minDeltaDTrkPt;    // TrkPt with min dist diff
    const TrkPt *minDeltaTTrkPt;    // TrkPt with min time diff
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

// Metric used for the SMA
typedef enum SmaMetric {
    elevation = 1,      // elevation
    grade = 2,          // grade
    power = 3           // power
} SmaMetric;

typedef struct CmdArgs {
    int argc;           // number of arguments
    char **argv;        // list of arguments
    const char *inFile; // input file name

    ActType actType;    // activity type for the output file
    int closeGap;       // close the time gap at the specified track point
    double maxGrade;    // max grade allowed (in %)
    double minGrade;    // min grade allowed (in %)
    const char *name;   // <name> tag
    FILE *outFile;      // output file
    OutFmt outFmt;      // format of the output data (csv, gpx)
    int outMask;        // bitmask of optional metrics to be included in the output
    Bool quiet;         // don't print any warning messages
    int rangeFrom;      // start point (inclusive)
    int rangeTo;        // end point (inclusive)
    TsFmt relTime;      // show relative timestamps in the specified format
    double setSpeed;    // speed to use to generate timestamps (in m/s)
    SmaMetric smaMetric;// metric to use for the SMA
    int smaWindow;      // size of the moving average window
    double startTime;   // start time for the activity
    Bool summary;       // show data summary
    Bool trim;          // trim points
    Bool verbatim;      // no data adjustments
} CmdArgs;

static const char *xmlHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

static const char *gpxHeader = "<gpx creator=\"gpxFileTool\" version=\"%d.%d\"\n"
                               "  xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/11.xsd\"\n"
                               "  xmlns:ns3=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\"\n"
                               "  xmlns=\"http://www.topografix.com/GPX/1/1\"\n"
                               "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:ns2=\"http://www.garmin.com/xmlschemas/GpxExtensions/v3\">\n";

static const char *tcxHeader = "<TrainingCenterDatabase\n"
                               "  xsi:schemaLocation=\"http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2 http://www.garmin.com/xmlschemas/TrainingCenterDatabasev2.xsd\"\n"
                               "  xmlns:ns5=\"http://www.garmin.com/xmlschemas/ActivityGoals/v1\"\n"
                               "  xmlns:ns3=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\"\n"
                               "  xmlns:ns2=\"http://www.garmin.com/xmlschemas/UserProfile/v2\"\n"
                               "  xmlns=\"http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2\"\n"
                               "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:ns4=\"http://www.garmin.com/xmlschemas/ProfileExtension/v1\">\n";

static const char *fgTcxSig = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?><TrainingCenterDatabase ";

static const double degToRad = (double) 0.01745329252;  // decimal degrees to radians
static const double earthMeanRadius = (double) 6372797.560856;  // in meters

static const char *help =
        "SYNTAX:\n"
        "    gpxFileTool [OPTIONS] <file> [<file2> ...]\n"
        "\n"
        "    When multiple input files are specified, the tool will attempt to\n"
        "    stitch them together into a single output file.\n"
        "\n"
        "OPTIONS:\n"
        "    --activity-type {ride|hike|run|walk|vride|other}\n"
        "        Specifies the type of activity in the output file. By default the\n"
        "        output file inherits the activity type of the input file.\n"
        "    --close-gap <point>\n"
        "        Close the time gap at the specified track point.\n"
        "    --help\n"
        "        Show this help and exit.\n"
        "    --max-grade <value>\n"
        "        Limit the maximum grade to the specified value. The elevation\n"
        "        values are adjusted accordingly.\n"
        "    --min-grade <value>\n"
        "        Limit the minimum grade to the specified value. The elevation\n"
        "        values are adjusted accordingly.\n"
        "    --name <name>\n"
        "        String to use for the <name> tag of the track in the output\n"
        "        file.\n"
        "    --output-file <name>\n"
        "        Write the output data into the specified file. If not specified\n"
        "        the output data is written to standard output.\n"
        "    --output-filter <mask>\n"
        "        A bit mask that specifies the set of optional metrics to be\n"
        "        suppressed from the output. By default, all available optional\n"
        "        metrics are included in the output.\n"
        "            0x01 - Ambient Temperature\n"
        "            0x02 - Cadence\n"
        "            0x04 - Heart Rate\n"
        "            0x08 - Power\n"
        "    --output-format {csv|gpx|shiz|tcx}\n"
        "        Specifies the format of the output data.\n"
        "    --quiet\n"
        "        Suppress all warning messages.\n"
        "    --range <a,b>\n"
        "        Limit the track points to be processed to the range between point\n"
        "        'a' and point 'b', inclusive.\n"
        "    --rel-time {sec|hms}\n"
        "        Use relative timestamps in the CSV output, using the specified\n"
        "        format.\n"
        "    --set-speed <avg-speed>\n"
        "        Use the specified average speed value (in km/h) to generate missing\n"
        "        timestamps, or to replace the existing timestamps, in the input file.\n"
        "    --sma-metric {elevation|grade|power}\n"
        "        Specifies the metric to be smoothed out by the Simple Moving Average.\n"
        "    --sma-window <size>\n"
        "        Size of the window used to compute the Simple Moving Average\n"
        "        of the selected values, in order to smooth them out. It must be\n"
        "        an odd value.\n"
        "    --start-time <time>\n"
        "        Start time for the activity (in UTC time). The timestamp of each\n"
        "        point is adjusted accordingly. Format is: 2018-01-22T10:01:10Z.\n"
        "    --summary\n"
        "        Print only a summary of the activity metrics in human-readable\n"
        "        form and exit.\n"
        "    --trim\n"
        "        Trim all the points in the specified range. The timestamps of\n"
        "        the points after point 'b' are adjusted accordingly, to avoid\n"
        "        a discontinuity in the time sequence.\n"
        "    --verbatim\n"
        "        Process the input file(s) verbatim, without making any adjust-\n"
        "        ments to the data.\n"
        "    --version\n"
        "        Show version information and exit.";

static __inline__ double mToKm(double m) { return (m / 1000.0); }
static __inline__ double mpsToKph(double mps) { return (mps * 3.6); }

static void invalidArgument(const char *arg, const char *val)
{
    fprintf(stderr, "Invalid argument: %s %s\n", arg, (val != NULL) ? val : "");
}

static int parseArgs(int argc, char **argv, CmdArgs *pArgs)
{
    int numArgs, n;

    if (argc < 2) {
        fprintf(stderr, "Invalid syntax.  Use 'gpxFileTool --help' for more information.\n");
        return -1;
    }

    // By default include all optional metrics in the output
    pArgs->outMask = SD_ALL;

    for (n = 1, numArgs = argc -1; n <= numArgs; n++) {
        const char *arg;
        const char *val;

        arg = argv[n];

        if (strcmp(arg, "--help") == 0) {
            fprintf(stdout, "%s\n", help);
            exit(0);
        }  else if (strcmp(arg, "--activity-type") == 0) {
            val = argv[++n];
            if (strcmp(val, "ride") == 0) {
                pArgs->actType = ride;
            } else if (strcmp(val, "hike") == 0) {
                pArgs->actType = hike;
            } else if (strcmp(val, "run") == 0) {
                pArgs->actType = run;
            } else if (strcmp(val, "walk") == 0) {
                pArgs->actType = walk;
            } else if (strcmp(val, "vride") == 0) {
                pArgs->actType = vride;
            } else if (strcmp(val, "other") == 0) {
                pArgs->actType = other;
            } else {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--close-gap") == 0) {
            val = argv[++n];
            if (sscanf(val, "%d", &pArgs->closeGap) != 1) {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--max-grade") == 0) {
            val = argv[++n];
            if (sscanf(val, "%le", &pArgs->maxGrade) != 1) {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--min-grade") == 0) {
            val = argv[++n];
            if (sscanf(val, "%le", &pArgs->minGrade) != 1) {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--name") == 0) {
            val = argv[++n];
            if ((pArgs->name = strdup(val)) == NULL) {
                fprintf(stderr, "Can't copy name argument: %s\n", val);
                return -1;
            }
        } else if (strcmp(arg, "--output-file") == 0) {
            val = argv[++n];
            if ((pArgs->outFile = fopen(val, "w")) == NULL) {
                fprintf(stderr, "Can't open output file %s (%s)\n", val, strerror(errno));
                return -1;
            }
        } else if (strcmp(arg, "--output-filter") == 0) {
            val = argv[++n];
            int mask = 0;
            if (sscanf(val, "0x%x", &mask) != 1) {
                invalidArgument(arg, val);
                return -1;
            }
            pArgs->outMask = ~mask; // switch suppress mask to include mask
        } else if (strcmp(arg, "--output-format") == 0) {
            val = argv[++n];
            if (strcmp(val, "csv") == 0) {
                pArgs->outFmt = csv;
            } else if (strcmp(val, "gpx") == 0) {
                pArgs->outFmt = gpx;
            } else if (strcmp(val, "shiz") == 0) {
                pArgs->outFmt = shiz;
            } else if (strcmp(val, "tcx") == 0) {
                pArgs->outFmt = tcx;
            } else {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--quiet") == 0) {
            pArgs->quiet = true;
        } else if (strcmp(arg, "--range") == 0) {
            val = argv[++n];
            if (sscanf(val, "%d,%d", &pArgs->rangeFrom, &pArgs->rangeTo) != 2) {
                invalidArgument(arg, val);
                return -1;
            }
            if ((pArgs->rangeFrom < 1) || (pArgs->rangeFrom >= pArgs->rangeTo)) {
                fprintf(stderr, "Invalid TrkPt range %d,%d\n", pArgs->rangeFrom, pArgs->rangeTo);
                return -1;
            }
        } else if (strcmp(arg, "--rel-time") == 0) {
            val = argv[++n];
            if (strcmp(val, "sec") == 0) {
                pArgs->relTime = sec;
            } else if (strcmp(val, "hms") == 0) {
                pArgs->relTime = hms;
            } else {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--set-speed") == 0) {
            val = argv[++n];
            if (sscanf(val, "%le", &pArgs->setSpeed) != 1) {
                invalidArgument(arg, val);
                return -1;
            }
            pArgs->setSpeed = (pArgs->setSpeed / 3.6);  // convert from km/h to m/s
        } else if (strcmp(arg, "--sma-metric") == 0) {
            val = argv[++n];
            if (strcmp(val, "elevation") == 0) {
                pArgs->smaMetric = elevation;
            } else if (strcmp(val, "grade") == 0) {
                pArgs->smaMetric = grade;
            } else if (strcmp(val, "power") == 0) {
                pArgs->smaMetric = power;
            } else {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--sma-window") == 0) {
            val = argv[++n];
            if ((sscanf(val, "%d", &pArgs->smaWindow) != 1) ||
                ((pArgs->smaWindow % 2) == 0)) {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--start-time") == 0) {
            val = argv[++n];
            struct tm brkDwnTime = {0};
            time_t time0;
            if (strcmp(val, "now") == 0) {
                time0 = time(NULL);
            } else if (strptime(val, "%Y-%m-%dT%H:%M:%S", &brkDwnTime) != NULL) {
                time0 = mktime(&brkDwnTime);
            } else {
                invalidArgument(arg, val);
                return -1;
            }
            pArgs->startTime = (double) time0;
        } else if (strcmp(arg, "--summary") == 0) {
            pArgs->summary = true;
            pArgs->relTime = sec;   // force relative timestamps
        } else if (strcmp(arg, "--trim") == 0) {
            pArgs->trim = true;
        } else if (strcmp(arg, "--verbatim") == 0) {
            pArgs->verbatim = true;
        } else if (strcmp(arg, "--version") == 0) {
            fprintf(stdout, "Version %d.%d %s\n", progVerMajor, progVerMinor, buildInfo);
            exit(0);
        } else if (strncmp(arg, "--", 2) == 0) {
            fprintf(stderr, "Invalid option: %s\nUse --help for the list of supported options.\n", arg);
            return -1;
        } else {
            // Assume it's the input file(s)
            break;
        }
    }

    pArgs->argc = argc;
    pArgs->argv = argv;

    if (pArgs->outFile == NULL) {
        // By default send output to stdout
        pArgs->outFile = stdout;
    }

    if ((pArgs->smaWindow != 0) && (pArgs->smaMetric == 0)) {
        // By default run the SMA over the elevation value
        pArgs->smaMetric = elevation;
    }

    return n;
}

static const char *fmtTrkPtIdx(const TrkPt *pTrkPt)
{
    static char fmtBuf[1024];

    snprintf(fmtBuf, sizeof (fmtBuf), "%s:%u", pTrkPt->inFile, pTrkPt->lineNum);

    return fmtBuf;
}

static void printTrkPt(TrkPt *p)
{
    fprintf(stderr, "TrkPt #%u at %s {\n", p->index, fmtTrkPtIdx(p));
    fprintf(stderr, "  latitude=%.10lf longitude=%.10lf elevation=%.10lf time=%.3lf distance=%.10lf speed=%.10lf dist=%.10lf run=%.10lf rise=%.10lf grade=%.2lf\n",
            p->latitude, p->longitude, p->elevation, p->timestamp, p->distance, p->speed, p->dist, p->run, p->rise, p->grade);
    fprintf(stderr, "}\n");
}

// Dump the specified number of track points before and
// after the given TrkPt.
static void dumpTrkPts(GpsTrk *pTrk, TrkPt *p, int numPtsBefore, int numPtsAfter)
{
    int i;
    TrkPt *tp;

    // Rewind numPtsBefore points...
    for (i = 0, tp = p; (i < numPtsBefore) && (tp != NULL); i++) {
        tp = TAILQ_PREV(tp, TrkPtList, tqEntry);
    }

    // Points before the given point
    while (tp != p) {
        printTrkPt(tp);
        tp = TAILQ_NEXT(tp, tqEntry);
    }

    // The point in question
    printTrkPt(p);

    // Points after the given point
    for (i = 0, tp = TAILQ_NEXT(p, tqEntry); (i < numPtsAfter) && (tp != NULL); i++, tp = TAILQ_NEXT(tp, tqEntry)) {
        printTrkPt(tp);
    }
}

static int getLine(FILE *fp, char *lineBuf, size_t bufLen, int lineNum)
{
    while (true) {
        if (fgets(lineBuf, bufLen, fp) == NULL)
            return -1;

        lineNum++;

        //fprintf(stdout, "%u: %s", lineNum, lineBuf);

        // Skip comment lines
        if (strstr(lineBuf, "<!--") == NULL)
            break;
    }

    return lineNum;
}

static void spongExit(const char *msg, const char *inFile, int lineNum, const char *lineBuf)
{
    fprintf(stderr, "SPONG! %s %s:%u \"%s\"\n", msg, inFile, lineNum, lineBuf);
    exit(-1);
}

static void noActTrkPt(const char *inFile, int lineNum, const char *lineBuf)
{
    fprintf(stderr, "SPONG! No active TrkPt !!! %s:%u \"%s\"\n", inFile, lineNum, lineBuf);
    exit(-1);
}

static TrkPt *newTrkPt(int index, const char *inFile, int lineNum)
{
    TrkPt *pTrkPt;

    if ((pTrkPt = calloc(1, sizeof(TrkPt))) == NULL) {
        fprintf(stderr, "Failed to alloc TrkPt object !!!\n");
        return NULL;
    }

    pTrkPt->index = index;
    pTrkPt->inFile = inFile;
    pTrkPt->lineNum = lineNum;
    pTrkPt->elevation = nilElev;

    return pTrkPt;
}

// Parse the GPX file and create a list of Track Points (TrkPt's).
// Notice that the number and format of each metric included in
// the TrkPt's can depend on the application which created the GPX
// file: e.g. Garmin, Strava, RWGPS, etc.  Below you can see the
// exact same Trkpt as created by each of these apps:
//
// Garmin Connect:
//
//   <trkpt lat="43.67811075411736965179443359375" lon="-114.31225128471851348876953125">
//     <ele>1829</ele>
//     <time>2022-03-20T20:40:26.000Z</time>
//     <extensions>
//       <ns3:TrackPointExtension>
//         <ns3:atemp>7.0</ns3:atemp>
//         <ns3:hr>146</ns3:hr>
//         <ns3:cad>95</ns3:cad>
//       </ns3:TrackPointExtension>
//     </extensions>
//   </trkpt>
//
// Strava:
//
//   <trkpt lat="43.6781110" lon="-114.3122510">
//    <ele>1829.0</ele>
//    <time>2022-03-20T20:40:26Z</time>
//    <extensions>
//     <power>173</power>
//     <gpxtpx:TrackPointExtension>
//      <gpxtpx:atemp>7</gpxtpx:atemp>
//      <gpxtpx:hr>146</gpxtpx:hr>
//      <gpxtpx:cad>95</gpxtpx:cad>
//     </gpxtpx:TrackPointExtension>
//    </extensions>
//   </trkpt>
//
// RWGPS:
//
//   <trkpt lat="43.678112" lon="-114.312248">
//     <ele>1829.0</ele>
//     <time>2022-03-20T20:40:26Z</time>
//     <extensions>
//       <gpxdata:hr>146</gpxdata:hr>
//       <gpxdata:cadence>95</gpxdata:cadence>
//     </extensions>
//   </trkpt>
//
static int parseGpxFile(CmdArgs *pArgs, GpsTrk *pTrk)
{
    FILE *fp;
    TrkPt *pTrkPt = NULL;
    const char *inFile = pArgs->inFile;
    int lineNum = 0;
    int metaData = 0;
    static char lineBuf[4096];
    size_t bufLen = sizeof (lineBuf);

    // Open the GPX file for reading
    if ((fp = fopen(inFile, "r")) == NULL) {
        fprintf(stderr, "Failed to open input file %s\n", inFile);
        return -1;
    }

    // Validate the input file. Expected format is:
    //
    // <?xml ...>
    // <gpx ...>
    //   .
    //   .
    //   .
    // </gpx>
    //
    lineNum = getLine(fp, lineBuf, bufLen, lineNum);
    if ((lineNum < 0) ||
        (strstr(lineBuf, "<?xml ") == NULL)) {
        fprintf(stderr, "Input file is not an XML file !!!\n");
        return -1;
    }
    lineNum = getLine(fp, lineBuf, bufLen, lineNum);
    if ((lineNum < 0) ||
        (strstr(lineBuf, "<gpx ") == NULL)) {
        fprintf(stderr, "Input file is not a recognized GPX file !!!\n");
        return -1;
    }

    // Process one line at a time, looking for <trkpt> ... </trkpt>
    // blocks that define each individual track point.
    while ((lineNum = getLine(fp, lineBuf, bufLen, lineNum)) != -1) {
        double latitude, longitude, elevation;
        struct tm brkDwnTime = {0};
        int type, ambTemp, cadence, heartRate, power;
        const char *p;

        // Ignore the metadata
        if (strstr(lineBuf, "<metadata>") != NULL) {
            metaData++;
            continue;
        } else if (strstr(lineBuf, "</metadata>") != NULL) {
            metaData--;
            continue;
        } else if (metaData) {
            continue;
        }

        if (sscanf(lineBuf, " <type>%d</type>", &type) == 1) {
            // Set the activity type
            pTrk->type = type;
        } else if ((sscanf(lineBuf, " <trkpt lat=\"%le\" lon=\"%le\">", &latitude, &longitude) == 2) ||
                   (sscanf(lineBuf, " <trkpt lon=\"%le\" lat=\"%le\">", &longitude, &latitude) == 2)) {
            if (pTrkPt != NULL) {
                // Hu?
                spongExit("Nested <trkpt> block !!!", inFile, lineNum, lineBuf);
            }

            // Alloc and init new TrkPt object
            if ((pTrkPt = newTrkPt(pTrk->numTrkPts++, inFile, lineNum)) == NULL) {
                fprintf(stderr, "Failed to create TrkPt object !!!\n");
                return -1;
            }

            pTrkPt->latitude = latitude;
            pTrkPt->longitude = longitude;
        } else if (sscanf(lineBuf, " <ele>%le</ele>", &elevation) == 1) {
            // Got the elevation!
            if (pTrkPt == NULL) {
                // Hu?
                noActTrkPt(inFile, lineNum, lineBuf);
            }
            pTrkPt->elevation = elevation;
        } else if ((p = strptime(lineBuf, " <time>%Y-%m-%dT%H:%M:%S", &brkDwnTime)) != NULL) {
            time_t timeStamp;
            int ms = 0;

            // Got the time!
            if (pTrkPt == NULL) {
                // Hu?
                noActTrkPt(inFile, lineNum, lineBuf);
            }

            // Convert to seconds since the Epoch
            timeStamp = mktime(&brkDwnTime);

            // If present, read the millisec portion
            if (sscanf(p, ".%d", &ms) == 1) {
                if ((ms < 0) || (ms > 999)) {
                    fprintf(stderr, "TrkPt %s has an invalid millisec value %d in its timestamp !!!\n", fmtTrkPtIdx(pTrkPt), ms);
                    return -1;
                }
            }

            pTrkPt->timestamp = (double) timeStamp + ((double) ms / 1000.0);  // sec.millisec since the Epoch
        } else if (sscanf(lineBuf, " <power>%d</power>", &power) == 1) {
            // Got the power!
            if (pTrkPt == NULL) {
                // Hu?
                noActTrkPt(inFile, lineNum, lineBuf);
            }
            pTrkPt->power = power;
            pTrk->inMask |= SD_POWER;
        } else if ((sscanf(lineBuf, " <gpxdata:atemp>%d</gpxdata:atemp>", &ambTemp) == 1) ||
                   (sscanf(lineBuf, " <gpxtpx:atemp>%d</gpxtpx:atemp>", &ambTemp) == 1) ||
                   (sscanf(lineBuf, " <ns3:atemp>%d</ns3:atemp>", &ambTemp) == 1)) {
            // Got the ambient temperature!
            if (pTrkPt == NULL) {
                // Hu?
                noActTrkPt(inFile, lineNum, lineBuf);
            }
            pTrkPt->ambTemp = ambTemp;
            pTrk->inMask |= SD_ATEMP;
        } else if ((sscanf(lineBuf, " <gpxdata:cadence>%d</gpxdata:cadence>", &cadence) == 1) ||
                   (sscanf(lineBuf, " <gpxtpx:cad>%d</gpxtpx:cad>", &cadence) == 1) ||
                   (sscanf(lineBuf, " <ns3:cad>%d</ns3:cad>", &cadence) == 1)) {
            // Got the cadence!
            if (pTrkPt == NULL) {
                // Hu?
                noActTrkPt(inFile, lineNum, lineBuf);
            }
            pTrkPt->cadence = cadence;
            pTrk->inMask |= SD_CADENCE;
        } else if ((sscanf(lineBuf, " <gpxdata:hr>%d</gpxdata:hr>", &heartRate) == 1) ||
                   (sscanf(lineBuf, " <gpxtpx:hr>%d</gpxtpx:hr>", &heartRate) == 1) ||
                   (sscanf(lineBuf, " <ns3:hr>%d</ns3:hr>", &heartRate) == 1)) {
            // Got the heart rate!
            if (pTrkPt == NULL) {
                // Hu?
                noActTrkPt(inFile, lineNum, lineBuf);
            }
            pTrkPt->heartRate = heartRate;
            pTrk->inMask |= SD_HR;
        } else if (strstr(lineBuf, "</trkpt>") != NULL) {
            // End of Track Point!
            if (pTrkPt == NULL) {
                // Hu?
                noActTrkPt(inFile, lineNum, lineBuf);
            }

            // Insert track point at the tail of the queue and update
            // the TrkPt count.
            TAILQ_INSERT_TAIL(&pTrk->trkPtList, pTrkPt, tqEntry);

            pTrkPt = NULL;
        } else {
            // Ignore this line...
        }
    }

    // If no explicit output format has been specified,
    // use the same format as the input file.
    if (pArgs->outFmt == nil) {
        pArgs->outFmt = gpx;
    }

    fclose(fp);

    return 0;
}

// Parse the TCX file and create a list of Track Points (TrkPt's).
// Notice that the number and format of each metric included in
// the TrkPt's can depend on the application which created the TCX
// file: e.g. Garmin, Strava, RWGPS, etc.  Below you can see the
// exact same Trkpt as created by each of these apps:
//
// Garmin Connect:
//
//  <Trackpoint>
//    <Time>2022-03-20T20:40:26.000Z</Time>
//    <Position>
//      <LatitudeDegrees>43.67811075411737</LatitudeDegrees>
//      <LongitudeDegrees>-114.31225128471851</LongitudeDegrees>
//    </Position>
//    <AltitudeMeters>1829.0</AltitudeMeters>
//    <DistanceMeters>19335.130859375</DistanceMeters>
//    <HeartRateBpm>
//      <Value>146</Value>
//    </HeartRateBpm>
//    <Cadence>95</Cadence>
//    <Extensions>
//      <ns3:TPX>
//        <ns3:Speed>5.159999847412109</ns3:Speed>
//        <ns3:Watts>173</ns3:Watts>
//      </ns3:TPX>
//    </Extensions>
//  </Trackpoint>
//
// Strava:
//
//      <Trackpoint>
//        <Time>2022-04-03T19:32:02Z</Time>
//        <Position>
//          <LatitudeDegrees>43.6230360</LatitudeDegrees>
//          <LongitudeDegrees>-114.3528450</LongitudeDegrees>
//        </Position>
//        <AltitudeMeters>1697.0</AltitudeMeters>
//        <DistanceMeters>0.0</DistanceMeters>
//        <HeartRateBpm>
//          <Value>93</Value>
//        </HeartRateBpm>
//        <Cadence>0</Cadence>
//        <Extensions>
//          <TPX xmlns="http://www.garmin.com/xmlschemas/ActivityExtension/v2">
//            <Speed>0.0</Speed>
//          </TPX>
//        </Extensions>
//      </Trackpoint>
//
// RWGPS:
//
//  <Trackpoint>
//    <Time>2022-03-20T20:40:26Z</Time>
//    <Position>
//      <LatitudeDegrees>43.678112</LatitudeDegrees>
//      <LongitudeDegrees>-114.312248</LongitudeDegrees>
//    </Position>
//    <AltitudeMeters>1829.0</AltitudeMeters>
//    <DistanceMeters>19335.13</DistanceMeters>
//    <HeartRateBpm>
//      <Value>146</Value>
//    </HeartRateBpm>
//    <Cadence>95</Cadence>
//    <Extensions>
//      <TPX xmlns="http://www.garmin.com/xmlschemas/ActivityExtension/v2">
//        <Watts>173</Watts>
//      </TPX>
//    </Extensions>
//  </Trackpoint>
//
// BigRing VR:
//
//  <Trackpoint>
//    <Time>2022-02-23T22:43:38.467Z</Time>
//    <Position>
//        <LatitudeDegrees>38.781644</LatitudeDegrees>
//        <LongitudeDegrees>-109.594449</LongitudeDegrees>
//    </Position>
//    <AltitudeMeters>1572.09</AltitudeMeters>
//    <DistanceMeters>22.0932</DistanceMeters>
//    <Cadence>63</Cadence>
//    <HeartRateBpm xsi:type="HeartRateInBeatsPerMinute_t">
//        <Value>111</Value>
//    </HeartRateBpm>
//    <Extensions>
//        <TPX xmlns="http://www.garmin.com/xmlschemas/ActivityExtension/v2">
//            <Speed>4.78374</Speed>
//            <Watts>191</Watts>
//        </TPX>
//    </Extensions>
//  </Trackpoint>
//
// FulGaz (after indenting XML):
//
//  <Trackpoint>
//    <Time>2022-03-12T16:02:56.0000000Z</Time>
//    <HeartRateBpm xsi:type="HeartRateInBeatsPerMinute_t">
//      <Value>153</Value>
//    </HeartRateBpm>
//    <Position>
//      <LatitudeDegrees>44.142261505127</LatitudeDegrees>
//      <LongitudeDegrees>5.37063407897949</LongitudeDegrees>
//    </Position>
//    <AltitudeMeters>1102.69995117188</AltitudeMeters>
//    <DistanceMeters>8420.0003053993</DistanceMeters>
//    <Cadence>72</Cadence>
//    <Extensions>
//      <TPX xmlns="http://www.garmin.com/xmlschemas/ActivityExtension/v2">
//        <Speed>5.12000409599994</Speed>
//        <Watts>154</Watts>
//      </TPX></Extensions>
//  </Trackpoint>

static int parseFulGazTcxFile(CmdArgs *pArgs, GpsTrk *pTrk, FILE *fp, char *lineBuf, size_t bufLen, int lineNum)
{
    fprintf(stderr, "TBD !!!\n");
    return 0;
}

static int parseNormalTcxFile(CmdArgs *pArgs, GpsTrk *pTrk, FILE *fp, char *lineBuf, size_t bufLen, int lineNum)
{
    TrkPt *pTrkPt = NULL;
    const char *inFile = pArgs->inFile;
    int trackBlock = false;

    if ((lineNum = getLine(fp, lineBuf, bufLen, lineNum)) < 0) {
        fprintf(stderr, "Can't read input file: %s\n", pArgs->inFile);
        return -1;
    }

    if (strstr(lineBuf, "<TrainingCenterDatabase") == NULL) {
        fprintf(stderr, "Input file is not a recognized TCX file: lineBuf=%s lineNum=%u\n", lineBuf, lineNum);
        return -1;
    }

    // Process one line at a time, looking for <Trackpoint> ... </Trackpoint>
    // blocks that define each individual track point.
    while ((lineNum = getLine(fp, lineBuf, bufLen, lineNum)) != -1) {
        if (pTrk->type == 0) {
            if (strstr(lineBuf, "<Activity Sport=\"Biking\">") != NULL) {
                pTrk->type = ride;
            } else if (strstr(lineBuf, "<Activity Sport=\"Hiking\">") != NULL) {
                pTrk->type = hike;
            } else if (strstr(lineBuf, "<Activity Sport=\"Running\">") != NULL) {
                pTrk->type = run;
            } else if (strstr(lineBuf, "<Activity Sport=\"Walking\">") != NULL) {
                pTrk->type = walk;
            } else if (strstr(lineBuf, "<Activity Sport=\"Other\">") != NULL) {
                pTrk->type = other;
            }
            if (pTrk->type != 0) {
                // Got the activity type/sport!
                continue;
            }
        }

        if (strstr(lineBuf, "<Track>") != NULL) {
            if (!trackBlock) {
                // Start of a <Track> ... </Track> block
                trackBlock = true;
            } else {
                // Hu?
                fprintf(stderr, "SPONG! Nested <Track> block !!! %s:%u \"%s\"\n", inFile, lineNum, lineBuf);
                return -1;
            }
        } else if (strstr(lineBuf, "</Track>") != NULL) {
            if (trackBlock) {
                // End of a <Track> ... </Track> block
                trackBlock = false;
            } else {
                // Hu?
                fprintf(stderr, "SPONG! Bogus </Track> tag !!! %s:%u \"%s\"\n", inFile, lineNum, lineBuf);
                return -1;
            }
        } else if (trackBlock) {
            double latitude, longitude, elevation;
            double distance, speed;
            struct tm brkDwnTime = {0};
            int cadence, heartRate, power;
            const char *p;

            if (strstr(lineBuf, "<Trackpoint>") != NULL) {
                if (pTrkPt != NULL) {
                    // Hu?
                    fprintf(stderr, "SPONG! Nested <Trackpoint> block !!! %s:%u \"%s\"\n", inFile, lineNum, lineBuf);
                    return -1;
                }

                // Alloc and init new TrkPt object
                if ((pTrkPt = newTrkPt(pTrk->numTrkPts++, inFile, lineNum)) == NULL) {
                    fprintf(stderr, "Failed to create TrkPt object !!!\n");
                    return -1;
                }
            } else if (sscanf(lineBuf, " <LatitudeDegrees>%le</LatitudeDegrees>", &latitude) == 1) {
                // Got the latitude!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }
                pTrkPt->latitude = latitude;
            } else if (sscanf(lineBuf, " <LongitudeDegrees>%le</LongitudeDegrees>", &longitude) == 1) {
                // Got the longitude!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }
                pTrkPt->longitude = longitude;
            } else if (sscanf(lineBuf, " <AltitudeMeters>%le</AltitudeMeters>", &elevation) == 1) {
                // Got the elevation!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }
                pTrkPt->elevation = elevation;
            } else if (sscanf(lineBuf, " <DistanceMeters>%le</DistanceMeters>", &distance) == 1) {
                // Got the distance!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }
                pTrkPt->distance = distance;
            } else if ((p = strptime(lineBuf, " <Time>%Y-%m-%dT%H:%M:%S", &brkDwnTime)) != NULL) {
                time_t timeStamp;
                int ms = 0;

                // Got the time!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }

                // Convert to seconds since the Epoch
                timeStamp = mktime(&brkDwnTime);

                // If present, read the millisec portion
                if (sscanf(p, ".%d", &ms) == 1) {
                    if ((ms < 0) || (ms > 999)) {
                        fprintf(stderr, "TrkPt %s has an invalid millisec value %d in timestamp !!!\n", fmtTrkPtIdx(pTrkPt), ms);
                        return -1;
                    }
                }

                pTrkPt->timestamp = (double) timeStamp + ((double) ms / 1000.0);  // sec+millisec since the Epoch
            } else if ((sscanf(lineBuf, " <ns3:Speed>%le<ns3:/Speed>", &speed) == 1) ||
                       (sscanf(lineBuf, " <Speed>%le</Speed>", &speed) == 1)) {
                // Got the speed!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }
                pTrkPt->speed = speed;
            } else if ((sscanf(lineBuf, " <ns3:Watts>%d<ns3:/Watts>", &power) == 1) ||
                       (sscanf(lineBuf, " <Watts>%d</Watts>", &power) == 1)) {
                // Got the power!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }
                pTrkPt->power = power;
                pTrk->inMask |= SD_POWER;
            } else if (sscanf(lineBuf, " <Cadence>%d</Cadence>", &cadence) == 1) {
                // Got the cadence!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }
                pTrkPt->cadence = cadence;
                pTrk->inMask |= SD_CADENCE;
            } else if (strstr(lineBuf, "<HeartRateBpm") != NULL) {
                lineNum = getLine(fp, lineBuf, bufLen, lineNum);
                if (sscanf(lineBuf, " <Value>%d</Value>", &heartRate) == 1) {
                    // Got the heart rate!
                    if (pTrkPt == NULL) {
                        // Hu?
                        noActTrkPt(inFile, lineNum, lineBuf);
                    }
                    pTrkPt->heartRate = heartRate;
                    pTrk->inMask |= SD_HR;
                }
            } else if (strstr(lineBuf, "</Trackpoint>") != NULL) {
                // End of Track Point!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }

                // Insert track point at the tail of the queue and update
                // the TrkPt count.
                TAILQ_INSERT_TAIL(&pTrk->trkPtList, pTrkPt, tqEntry);

                pTrkPt = NULL;
            } else {
                // Ignore this line...
            }
        }
    }

    return 0;
}

static int parseTcxFile(CmdArgs *pArgs, GpsTrk *pTrk)
{
    FILE *fp;
    const char *inFile = pArgs->inFile;
    int lineNum = 0;
    static char lineBuf[1024];
    size_t bufLen = sizeof (lineBuf);
    int s;

    // Open the TCX file for reading
    if ((fp = fopen(inFile, "r")) == NULL) {
        fprintf(stderr, "Failed to open input file %s\n", inFile);
        return -1;
    }

    // Validate the input file. The common format used by Garmin,
    // Strava, RideWithGps, etc. is:
    //
    // <?xml ...>
    // <TrainingCenterDatabase  ...>
    //   .
    //   .
    //   .
    // </TrainingCenterDatabase>
    //
    // However, the TCX activity files created by FulGaz have all
    // data collapsed into a single loooong line of text, that has
    // the following signature:
    //
    // <?xml version="1.0" encoding="UTF-8" standalone="no" ?><TrainingCenterDatabase ...
    //
    if ((lineNum = getLine(fp, lineBuf, bufLen, lineNum)) < 0) {
        fprintf(stderr, "Can't read input file: %s\n", inFile);
        return -1;
    }
    if (strstr(lineBuf, fgTcxSig) != NULL) {
        s = parseFulGazTcxFile(pArgs, pTrk, fp, lineBuf, bufLen, lineNum);
    } else if (strstr(lineBuf, "<?xml ") != NULL) {
        s = parseNormalTcxFile(pArgs, pTrk, fp, lineBuf, bufLen, lineNum);
    } else {
        fprintf(stderr, "Input file is not an XML file: lineBuf=%s lineNum=%u\n", lineBuf, lineNum);
        return -1;
    }

    // If no explicit output format has been specified,
    // use the same format as the input file.
    if (pArgs->outFmt == nil) {
        pArgs->outFmt = tcx;
    }

    fclose(fp);

    return s;
}

static TrkPt *nxtTrkPt(TrkPt **p1, TrkPt *p2)
{
    *p1 = p2;
    return TAILQ_NEXT(p2, tqEntry);
}

static TrkPt *remTrkPt(GpsTrk *pTrk, TrkPt *p)
{
    TrkPt *nxt = TAILQ_NEXT(p, tqEntry);
    TAILQ_REMOVE(&pTrk->trkPtList, p, tqEntry);
    free(p);
    return nxt;
}

static int checkTrkPts(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt
    Bool discTrkPt = false;
    Bool trimTrkPts = false;
    double trimmedTime = 0.0;
    double trimmedDistance = 0.0;
    TrkPt *p0 = NULL;

    while (p2 != NULL) {
        // Discard any duplicate points, and any points
        // in the specified trim range ...
        discTrkPt = false;

        // Without elevation data, there isn't much we can do!
        if (p2->elevation == nilElev) {
            fprintf(stderr, "ERROR: TrkPt #%d (%s) is missing its elevation data !\n", p2->index, fmtTrkPtIdx(p2));
            return -1;
        }

        // The only case when we allow TrkPt's without a
        // timestamp is when we are processing a "route"
        // file, to convert it into a "ride" file, in
        // which case a desired average speed should have
        // been specified, in order to compute the timing
        // data from this speed and the distance...
        if ((p2->timestamp == 0.0) && (pArgs->setSpeed == 0.0)) {
            fprintf(stderr, "ERROR: TrkPt #%d (%s) is missing its date/time data !\n", p2->index, fmtTrkPtIdx(p2));
            return -1;
        }

        // Unless the user requested to process the file
        // verbatim, let's do some checks and clean up...
        if (!pArgs->verbatim) {
            // Some GPX tracks may have duplicate TrkPt's. This
            // can happen when the file has multiple laps, and
            // the last point in lap N is the same as the first
            // point in lap N+1.
            if ((p2->latitude == p1->latitude) &&
                (p2->longitude == p1->longitude) &&
                (p2->elevation == p1->elevation)) {
                if (!pArgs->quiet) {
                    fprintf(stderr, "INFO: Discarding duplicate TrkPt #%d (%s) !\n", p2->index, fmtTrkPtIdx(p2));
                }
                pTrk->numDupTrkPts++;
                discTrkPt = true;
            }

            // Timestamps should increase monotonically
            if ((p2->timestamp != 0.0) && (p2->timestamp <= p1->timestamp)) {
                if (!pArgs->quiet) {
                    fprintf(stderr, "INFO: TrkPt #%d (%s) has a non-increasing timestamp value: %.3lf !\n",
                            p2->index, fmtTrkPtIdx(p2), p2->timestamp);
                }

                // Discard as a dummy
                pTrk->numDiscTrkPts++;
                discTrkPt = true;
            }

            // Distance should increase monotonically
            if ((p2->distance != 0) && (p2->distance <= p1->distance)) {
                if (!pArgs->quiet) {
                    fprintf(stderr, "INFO: TrkPt #%d (%s) has a non-increasing distance value: %.3lf !\n",
                            p2->index, fmtTrkPtIdx(p2), p2->distance);
                }

                // Discard as a dummy
                pTrk->numDiscTrkPts++;
                discTrkPt = true;
            }
        }

        // Do we need to trim out this TrkPt?
        if (pArgs->trim) {
            if (p2->index == pArgs->rangeFrom) {
                // Start trimming
                if (!pArgs->quiet) {
                    fprintf(stderr, "INFO: start trimming at TrkPt #%d (%s)\n", p2->index, fmtTrkPtIdx(p2));
                }
                trimTrkPts = true;
                pTrk->numTrimTrkPts++;
                discTrkPt = true;
                p0 = p1;    // set baseline
            } else if (p2->index == pArgs->rangeTo) {
                // Stop trimming
                if (!pArgs->quiet) {
                    fprintf(stderr, "INFO: stop trimming at TrkPt #%d (%s)\n", p2->index, fmtTrkPtIdx(p2));
                }
                trimTrkPts = false;
                trimmedTime = p2->timestamp - p0->timestamp;    // total time trimmed out
                trimmedDistance = p2->distance - p0->distance;  // total distance trimmed out
                pTrk->numTrimTrkPts++;
                discTrkPt = true;
            } else if (trimTrkPts) {
                // Trim this point
                pTrk->numTrimTrkPts++;
                discTrkPt = true;
            }
        }

        // Discard?
        if (discTrkPt) {
            // Remove this TrkPt from the list
            p2 = remTrkPt(pTrk, p2);
        } else {
            // If we trimmed out some previous TrkPt's, then we
            // need to adjust the timestamp and distance values
            // of this TrkPt so as to "close the gap".
            if (p0 != NULL) {
                p2->timestamp -= trimmedTime;
                p2->distance -= trimmedDistance;
            }
            p2 = nxtTrkPt(&p1, p2);
        }
    }

    return 0;
}

static int closeTimeGap(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt
    Bool trkPtFound = false;
    double timeGap = 0.0;

    while (p2 != NULL) {
        if (!trkPtFound && (p2->index == pArgs->closeGap)) {
            timeGap = p2->timestamp - p1->timestamp - 1;
            trkPtFound = true;
            if (!pArgs->quiet) {
                fprintf(stderr, "INFO: Closing %.3lf s time gap at TrkPt #%u\n", timeGap, p2->index);
            }
        }

        if (trkPtFound) {
            p2->timestamp -= timeGap;
        }

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
}

// Compute the distance (in meters) between two track points
// using the Haversine formula. See below for the details:
//
//   https://en.wikipedia.org/wiki/Haversine_formula
//
static double compDistance(const TrkPt *p1, const TrkPt *p2)
{
    const double two = (double) 2.0;
    double phi1 = p1->latitude * degToRad;  // p1's latitude in radians
    double phi2 = p2->latitude * degToRad;  // p2's latitude in radians
    double deltaPhi = (phi2 - phi1);        // latitude diff in radians
    double deltaLambda = (p2->longitude - p1->longitude) * degToRad;   // longitude diff in radians
    double a = sin(deltaPhi / two);
    double b = sin(deltaLambda / two);
    double h = (a * a) + cos(phi1) * cos(phi2) * (b * b);

    assert(h >= 0.0);

    return (two * earthMeanRadius * asin(sqrt(h)));
}

// Compute the bearing (in decimal degrees) between two track
// points.  See below for the details:
//
//   https://www.movable-type.co.uk/scripts/latlong.html
//
static double compBearing(const TrkPt *p1, const TrkPt *p2)
{
    double phi1 = p1->latitude * degToRad;  // p1's latitude in radians
    double phi2 = p2->latitude * degToRad;  // p2's latitude in radians
    double deltaLambda = (p2->longitude - p1->longitude) * degToRad;   // longitude diff in radians
    double x = sin(deltaLambda) * cos(phi2);
    double y = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(deltaLambda);
    double theta = atan2(x, y);  // in radians

    return fmod((theta / degToRad + 360.0), 360.0); // in degrees decimal (0-359.99)
}

// Given a fixed distance (dist) figure out what the
// elevation difference (rise) should be, in order to
// get the desired grade value, and adjust the elevation
// value accordingly.
//
//   rise^2 = dist^2 / (1 + (1 / grade^2));
//
static void adjElevation(GpsTrk *pTrk, TrkPt *p1, TrkPt *p2)
{
    double grade = (p2->grade / 100.0); // desired grade in decimal (0.00 .. 1.00)
    double grade2 = (grade * grade);    // grade squared
    double dist2 = (p2->dist * p2->dist);   // dist squared
    double rise = sqrt(dist2 / (1.0 + (1.0 / grade2)));
    double adjElev;

    if (p2->rise >= 0.0) {
        p2->rise = rise;
    } else {
        p2->rise = (0.0 - rise);
    }
    adjElev = p1->elevation + p2->rise;
    if (adjElev != p2->elevation) {
        p2->elevation = adjElev;
        pTrk->numElevAdj++;
    }
}

static int compDataPhase1(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt

    // Compute the distance, elevation diff, speed, and grade
    // between each pair of points...
    while (p2 != NULL) {
        double absRise; // always positive!

        // Compute the elevation difference (can be negative)
        p2->rise = p2->elevation - p1->elevation;

        // The "rise" is always positive!
        absRise = fabs(p2->rise);

        // TCX files include the <DistanceMeters> metric
        // which is the distance (in meters) from the start
        // up to the given point.  For GPX files, we need to
        // compute the distance between consecutive points
        // using their GPS data.
        if (p2->distance != 0.0) {
            if ((p2->dist = p2->distance - p1->distance) == 0.0) {
                // Stopped?
                if (!pArgs->verbatim) {
                    if (!pArgs->quiet) {
                        fprintf(stderr, "WARNING: TrkPt #%d has a null distance value !\n", p2->index);
                        printTrkPt(p2);
                    }

                    // Skip and delete this TrkPt
                    p2 = remTrkPt(pTrk, p2);
                    pTrk->numDiscTrkPts++;
                } else {
                    // Move on to the next point
                    p2 = nxtTrkPt(&p1, p2);
                }
                continue;
            }

            if (p2->dist > absRise) {
                // Compute the horizontal distance "run" using
                // Pythagoras's Theorem.
                p2->run = sqrt((p2->dist * p2->dist) - (absRise * absRise));
            } else {
                // Bogus data?
                p2->run = p2->dist;
            }
        } else {
            // Compute the horizontal distance "run" between
            // the two points, based on their latitude and
            // longitude values.
            if ((p2->run = compDistance(p1, p2)) == 0.0) {
                // Stopped?
                if (!pArgs->verbatim) {
                    if (!pArgs->quiet) {
                        fprintf(stderr, "WARNING: TrkPt #%d has a null run value !\n", p2->index);
                        printTrkPt(p2);
                    }

                    // Skip and delete this TrkPt
                    p2 = remTrkPt(pTrk, p2);
                    pTrk->numDiscTrkPts++;
                } else {
                    // Move on to the next point
                    p2 = nxtTrkPt(&p1, p2);
                }
                continue;
            }

            // Compute the actual distance traveled between
            // the two points.
            if (absRise == 0.0) {
                // Riding on the flats, dist equals run...
                p2->dist = p2->run;
            } else {
                // Use Pythagoras's Theorem to compute the
                // distance (hypotenuse)
                p2->dist = sqrt((p2->run * p2->run) + (absRise * absRise));
            }

            p2->distance = p1->distance + p2->dist;
        }

        // Paranoia?
        if (p2->distance < p1->distance) {
            fprintf(stderr, "SPONG! TrkPt #%u (%s) has a non-increasing distance !\n",
                    p2->index, fmtTrkPtIdx(p2));
            fprintf(stderr, "dist=%.10lf run=%.10lf absRise=%.10lf\n", p2->dist, p2->run, absRise);
            dumpTrkPts(pTrk, p2, 2, 0);
        }

        // Update the max dist value
        if (p2->dist > pTrk->maxDeltaD) {
            pTrk->maxDeltaD = p2->dist;
            pTrk->maxDeltaDTrkPt = p2;
        }

        // If needed, compute the time interval based on the
        // distance and the specified average speed.
        if (p2->timestamp == 0.0) {
            p2->deltaT = p2->dist / pArgs->setSpeed;
            p2->timestamp = p1->timestamp + p2->deltaT;
        }

        // Compute the time interval between the two points.
        // Typically fixed at 1-sec, but some GPS devices (e.g.
        // Garmin Edge) may use a "smart" recording mode that
        // can have several seconds between points, while
        // other devices (e.g. GoPro Hero) may record multiple
        // points each second. And when converting a GPX route
        // into a GPX ride, the time interval is arbitrary,
        // computed from the distance and the speed.
        p2->deltaT = (p2->timestamp - p1->timestamp);

        // Paranoia?
        if (p2->deltaT <= 0.0) {
            fprintf(stderr, "SPONG! TrkPt #%u (%s) has a non-increasing timestamp ! dist=%.10lf deltaT=%.3lf\n",
                    p2->index, fmtTrkPtIdx(p2), p2->dist, p2->deltaT);
            dumpTrkPts(pTrk, p2, 2, 0);
        }

        // Update the max time interval between two points
        if (p2->deltaT > pTrk->maxDeltaT) {
            pTrk->maxDeltaT = p2->deltaT;
            pTrk->maxDeltaTTrkPt = p2;
        }

        if (p2->speed == 0.0) {
            // Compute the speed as "distance over time"
            p2->speed = p2->dist / p2->deltaT;
        }

        // Update the total distance for the activity
        pTrk->distance += p2->dist;

        // Update the total time for the activity
        pTrk->time += p2->deltaT;

        // Compute the grade as "rise over run". Notice
        // that the grade value may get updated later.
        // Guard against points with run=0, which can
        // happen when using the "--verbose" option...
        if (p2->run != 0.0) {
            p2->grade = (p2->rise * 100.0) / p2->run;   // in [%]
        } else {
            p2->grade = p1->grade;  // carry over the previous grade value
        }

        // Compute the bearing
        p2->bearing = compBearing(p1, p2);

        // Update the activity's end time
        pTrk->endTime = p2->timestamp;

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
}

static double smaGetVal(TrkPt *p, SmaMetric smaMetric)
{
    if (smaMetric == elevation) {
        return (double) p->elevation;
    } else if (smaMetric == grade) {
        return (double) p->grade;
    } else {
        return (double) p->power;
    }
}

static double smaSetVal(TrkPt *p, SmaMetric smaMetric, double value)
{
    if (smaMetric == elevation) {
        p->elevation = value;
    } else if (smaMetric == grade) {
        p->grade = value;
    } else {
        p->power = (int) value;
    }

    return value;
}

// Compute the Simple Moving Average (SMA) of the specified
// metric at the given point. The SMA uses a window size of N
// points, where N is an odd value. The average is computed
// using the (N-1) values before the point, and the value of
// the given point.
static void compSma(GpsTrk *pTrk, TrkPt *p, SmaMetric smaMetric, int smaWindow)
{
    int i;
    int n = (smaWindow - 1);
    int numPoints = 0;
    double summ = 0.0;
    double value = smaGetVal(p, smaMetric);
    double smaVal;
    TrkPt *tp;

    // Points before the given point
    for (i = 0, tp = TAILQ_PREV(p, TrkPtList, tqEntry); (i < n) && (tp != NULL); i++, tp = TAILQ_PREV(tp, TrkPtList, tqEntry)) {
        summ += smaGetVal(tp, smaMetric);
        numPoints++;
    }

    // The given point
    summ += value;
    numPoints++;

    // SMA value
    smaVal = summ / numPoints;

    //fprintf(stderr, "%s: index=%d metric=%d before=%.3lf summ=%.3lf pts=%d after=%.3lf\n", __func__, p->index, smaMetric, value, summ, numPoints, smaVal);

    // Override the original value with the
    // computed SMA value.
    smaSetVal(p, smaMetric, smaVal);

    if (smaVal != value) {
        if (smaMetric == elevation) {
            // Recompute the grade using the adjusted
            // elevation value. Guard against points
            // with run=0, which can happen when using
            // the --verbose option...
            TrkPt *prev = TAILQ_PREV(p, TrkPtList, tqEntry);
            if (p->run != 0.0) {
                p->rise = p->elevation - prev->elevation;
                p->grade = (p->rise * 100.0) / p->run;  // in [%]
            } else {
                p->grade = prev->grade; // carry over the previous grade value
            }
        } else if (smaMetric == grade) {
            // Flag that this point had its grade adjusted
            p->adjGrade = true;
        }
    }
}

static Bool pointWithinRange(const CmdArgs *pArgs, const TrkPt *p)
{
    if (pArgs->rangeFrom == 0) {
        // No actual range specified, so all points
        // are within range...
        return true;
    }

    if ((p->index >= pArgs->rangeFrom) && (p->index <= pArgs->rangeTo)) {
        // Point is within specified range
        return true;
    }

    return false;
}

static void adjMaxGrade(GpsTrk *pTrk, CmdArgs *pArgs, TrkPt *p1, TrkPt *p2)
{
    if (!pArgs->quiet) {
        fprintf(stderr, "WARNING: TrkPt #%d (%s) has a grade of %.2lf%% that is above the max value %.2lf%% !\n",
                p2->index, fmtTrkPtIdx(p2), p2->grade, pArgs->maxGrade);
    }

    // Override original value with the max value
    p2->grade = pArgs->maxGrade;

    // Flag that this point had its grade adjusted
    p2->adjGrade = true;
}

static void adjMinGrade(GpsTrk *pTrk, CmdArgs *pArgs, TrkPt *p1, TrkPt *p2)
{
    if (!pArgs->quiet) {
        fprintf(stderr, "WARNING: TrkPt #%d (%s) has a grade of %.2lf%% that is below the min value %.2lf%% !\n",
                p2->index, fmtTrkPtIdx(p2), p2->grade, pArgs->minGrade);
    }

    // Override original value with the min value
    p2->grade = pArgs->minGrade;

    // Flag that this point had its grade adjusted
    p2->adjGrade = true;
}

static int compDataPhase2(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt
    double deltaG;

    while (p2 != NULL) {
        if (pointWithinRange(pArgs, p2)) {
            // Do we need to smooth out any values?
            if (pArgs->smaWindow != 0) {
                compSma(pTrk, p2, pArgs->smaMetric, pArgs->smaWindow);
            }

            // See if we need to limit the max grade values
            if ((pArgs->maxGrade != 0.0) && (p2->grade > pArgs->maxGrade)) {
                adjMaxGrade(pTrk, pArgs, p1, p2);
            }

            // See if we need to limit the min grade values
            if ((pArgs->minGrade != 0.0) && (p2->grade < pArgs->minGrade)) {
                adjMinGrade(pTrk, pArgs, p1, p2);
            }
        }

        // If necessary, correct the elevation value based
        // on the adjusted grade value. We need to adjust
        // the value of delatE, while the value of deltaP
        // remains invariant; i.e. the deltaP vector needs
        // to rotate along an arc so that the tanget of the
        // angle 'alpha' with the horizontal (run) results
        // in the adjusted/desired grade value.
        if (p2->adjGrade) {
            adjElevation(pTrk, p1, p2);
        }

        // Update the rolling elevation gain/loss values
        if (p2->rise >= 0.0) {
            pTrk->elevGain += p2->rise;
        } else {
            pTrk->elevLoss += fabs(p2->rise);
        }

        // Update the rolling cadence, heart rate, and power
        // values used to compute the activity averages.
        pTrk->cadence += p2->cadence;
        pTrk->heartRate += p2->heartRate;
        pTrk->power += p2->power;
        pTrk->temp += p2->ambTemp;

        deltaG = fabs(p2->grade - p1->grade);
        if (deltaG > pTrk->maxDeltaG) {
            pTrk->maxDeltaG = deltaG;
            pTrk->maxDeltaGTrkPt = p2;
        }

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
}

static int compDataPhase3(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt

    pTrk->minCadence = +999;
    pTrk->maxCadence = -999;
    pTrk->minHeartRate = +999;
    pTrk->maxHeartRate = -999;
    pTrk->minPower = +9999;
    pTrk->maxPower = -9999;
    pTrk->minSpeed = +999.9;
    pTrk->maxSpeed = -999.9;
    pTrk->minTemp = +999.9;
    pTrk->maxTemp = -999.9;
    pTrk->minGrade = +99.9;
    pTrk->maxGrade = -99.9;

    while (p2 != NULL) {
        // Update the min/max values
        if (pTrk->inMask & SD_CADENCE) {
            if (p2->cadence > pTrk->maxCadence) {
                 pTrk->maxCadence = p2->cadence;
                 pTrk->maxCadenceTrkPt = p2;
            } else if ((p2->cadence != 0) && (p2->cadence < pTrk->minCadence)) {
                pTrk->minCadence = p2->cadence;
                pTrk->minCadenceTrkPt = p2;
            }
        }

        if (pTrk->inMask & SD_HR) {
            if (p2->heartRate > pTrk->maxHeartRate) {
                 pTrk->maxHeartRate = p2->heartRate;
                 pTrk->maxHeartRateTrkPt = p2;
            } else if ((p2->heartRate != 0) && (p2->heartRate < pTrk->minHeartRate)) {
                pTrk->minHeartRate = p2->heartRate;
                pTrk->minHeartRateTrkPt = p2;
            }
        }

        if (pTrk->inMask & SD_POWER) {
            if (p2->power > pTrk->maxPower) {
                 pTrk->maxPower = p2->power;
                 pTrk->maxPowerTrkPt = p2;
            } else if ((p2->power != 0) && (p2->power < pTrk->minPower)) {
                pTrk->minPower = p2->power;
                pTrk->minPowerTrkPt = p2;
            }
        }

        if (p2->speed > pTrk->maxSpeed) {
             pTrk->maxSpeed = p2->speed;
             pTrk->maxSpeedTrkPt = p2;
        } else if ((p2->speed != 0) && (p2->speed < pTrk->minSpeed)) {
            pTrk->minSpeed = p2->speed;
            pTrk->minSpeedTrkPt = p2;
        }

        if (pTrk->inMask & SD_ATEMP) {
            if (p2->ambTemp > pTrk->maxTemp) {
                 pTrk->maxTemp = p2->ambTemp;
                 pTrk->maxTempTrkPt = p2;
            } else if (p2->ambTemp < pTrk->minTemp) {
                pTrk->minTemp = p2->ambTemp;
                pTrk->minTempTrkPt = p2;
            }
        }

        if (p2->grade > pTrk->maxGrade) {
             pTrk->maxGrade = p2->grade;
             pTrk->maxGradeTrkPt = p2;
        } else if (p2->grade < pTrk->minGrade) {
            pTrk->minGrade = p2->grade;
            pTrk->minGradeTrkPt = p2;
        }

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
}

static const char *fmtTimeStamp(time_t ts, TsFmt fmt)
{
    static char fmtBuf[64];

    if (fmt == hms) {
        time_t time = ts;
        int hr, min, sec;
        hr = time / 3600;
        min = (time - (hr * 3600)) / 60;
        sec = (time - (hr * 3600) - (min * 60));
        snprintf(fmtBuf, sizeof (fmtBuf), "%02d:%02d:%02d", hr, min, sec);
    } else {
        snprintf(fmtBuf, sizeof (fmtBuf), "%ld", ts);
    }

    return fmtBuf;
}

static void printSummary(GpsTrk *pTrk, CmdArgs *pArgs)
{
    time_t time;
    const TrkPt *p;

    fprintf(pArgs->outFile, "      numTrkPts: %d\n", pTrk->numTrkPts);
    fprintf(pArgs->outFile, "   numDupTrkPts: %d\n", pTrk->numDupTrkPts);
    fprintf(pArgs->outFile, "  numTrimTrkPts: %d\n", pTrk->numTrimTrkPts);
    fprintf(pArgs->outFile, "  numDiscTrkPts: %d\n", pTrk->numDiscTrkPts);
    fprintf(pArgs->outFile, "     numElevAdj: %d\n", pTrk->numElevAdj);

    // Date & time
    {
        double timeStamp;
        struct tm brkDwnTime = {0};
        char timeBuf[128];
        time_t dateAndTime;

        p = TAILQ_FIRST(&pTrk->trkPtList);
        timeStamp = (p->adjTime != 0) ? p->adjTime : p->timestamp;    // use the adjusted timestamp if there is one
        timeStamp += pTrk->timeOffset;
        dateAndTime = (time_t) timeStamp;  // sec only
        strftime(timeBuf, sizeof (timeBuf), "%Y-%m-%dT%H:%M:%S", gmtime_r(&dateAndTime, &brkDwnTime));
        fprintf(pArgs->outFile, "  dateAndTime: %s\n", timeBuf);
    }

    // Elapsed time
    time = pTrk->endTime - pTrk->startTime;
    fprintf(pArgs->outFile, "    elapsedTime: %s\n", fmtTimeStamp(time, hms));

    // Total time
    time = pTrk->time;
    fprintf(pArgs->outFile, "      totalTime: %s\n", fmtTimeStamp(time, hms));

    // Moving time
    time = pTrk->time - pTrk->stoppedTime;
    fprintf(pArgs->outFile, "     movingTime: %s\n", fmtTimeStamp(time, hms));

    // Stopped time
    time = pTrk->stoppedTime;
    fprintf(pArgs->outFile, "    stoppedTime: %s\n", fmtTimeStamp(time, hms));

    fprintf(pArgs->outFile, "       distance: %.10lf km\n", (pTrk->distance / 1000.0));
    fprintf(pArgs->outFile, "       elevGain: %.10lf m\n", pTrk->elevGain);
    fprintf(pArgs->outFile, "       elevLoss: %.10lf m\n", pTrk->elevLoss);

    // Max/Min/Avg values
    {
        p = pTrk->maxSpeedTrkPt;
        fprintf(pArgs->outFile, "       maxSpeed: %.3lf km/h @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km, deltaD = %.3lf m, deltaT = %.3lf s\n",
                mpsToKph(pTrk->maxSpeed), p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance), p->dist, p->deltaT);
        p = pTrk->minSpeedTrkPt;
        fprintf(pArgs->outFile, "       minSpeed: %.3lf km/h @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km, deltaD = %.3lf m, deltaT = %.3lf s\n",
                mpsToKph(pTrk->minSpeed), p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance), p->dist, p->deltaT);
        fprintf(pArgs->outFile, "       avgSpeed: %.3lf km/h\n", mpsToKph(pTrk->distance / pTrk->time));
    }

    {
        p = pTrk->maxGradeTrkPt;
        fprintf(pArgs->outFile, "       maxGrade: %.2lf%% @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km, run = %.3lf m, rise = %.3lf m\n",
                pTrk->maxGrade, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance), p->run, p->rise);
        p = pTrk->minGradeTrkPt;
        fprintf(pArgs->outFile, "       minGrade: %.2lf%% @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km, run = %.3lf m, rise = %.3lf m\n",
                pTrk->minGrade, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance), p->run, p->rise);
    }

    if (pTrk->inMask & SD_CADENCE) {
        p = pTrk->maxCadenceTrkPt;
        fprintf(pArgs->outFile, "     maxCadence: %d rpm @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxCadence, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        fprintf(pArgs->outFile, "     minCadence: %d rpm\n", pTrk->minCadence);
        fprintf(pArgs->outFile, "     avgCadence: %d rpm\n", (pTrk->cadence / pTrk->numTrkPts));
    }
    if (pTrk->inMask & SD_HR) {
        p = pTrk->maxHeartRateTrkPt;
        fprintf(pArgs->outFile, "          maxHR: %d bpm @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxHeartRate, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        fprintf(pArgs->outFile, "          minHR: %d bpm\n", pTrk->minHeartRate);
        fprintf(pArgs->outFile, "          avgHR: %d bpm\n", (pTrk->heartRate / pTrk->numTrkPts));
    }
    if (pTrk->inMask & SD_POWER) {
        p = pTrk->maxPowerTrkPt;
        fprintf(pArgs->outFile, "       maxPower: %d watts @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxPower, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        fprintf(pArgs->outFile, "       minPower: %d watts\n", pTrk->minPower);
        fprintf(pArgs->outFile, "       avgPower: %d watts\n", (pTrk->power / pTrk->numTrkPts));
    }
    if (pTrk->inMask & SD_ATEMP) {
        p = pTrk->maxTempTrkPt;
        fprintf(pArgs->outFile, "        maxTemp: %d C @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxTemp, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        fprintf(pArgs->outFile, "        minTemp: %d C\n", pTrk->minTemp);
        fprintf(pArgs->outFile, "        avgTemp: %d C\n", (pTrk->temp / pTrk->numTrkPts));
    }

    if ((p = pTrk->maxDeltaDTrkPt) != NULL) {
        fprintf(pArgs->outFile, "      maxDeltaD: %.3lf m @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxDeltaD, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
    }
    if ((p = pTrk->maxDeltaTTrkPt) != NULL) {
        fprintf(pArgs->outFile, "      maxDeltaT: %.3lf sec @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxDeltaT, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
    }
    if ((p = pTrk->maxDeltaGTrkPt) != NULL) {
        fprintf(pArgs->outFile, "      maxDeltaG: %.2lf%% @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxDeltaG, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
    }
}

static void printCsvFmt(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p;

    // Print column banner line
    fprintf(pArgs->outFile, "<inFile>,<line#>,<trkpt>,<time>,<lat>,<lon>,<ele>,");
    fprintf(pArgs->outFile, "<power>,<atemp>,<cadence>,<hr>,<deltaT>,<run>,<rise>,<dist>,<distance>,<speed>,<grade>,<deltaG>\n");

    TAILQ_FOREACH(p, &pTrk->trkPtList, tqEntry) {
        TrkPt *prev = TAILQ_PREV(p, TrkPtList, tqEntry);
        double timeStamp = (p->adjTime != 0.0) ? p->adjTime : p->timestamp;    // use the adjusted timestamp if there is one

        // Account for the time shift due to SMA
        if ((pArgs->smaWindow != 0) && (p->index < pArgs->smaWindow))
            continue;

        fprintf(pArgs->outFile, "%s,%d,%d,%s,%.10lf,%.10lf,%.10lf,",
                p->inFile,                      // <inFile>
                p->lineNum,                     // <line#>
                p->index,                       // <trkPt>
                fmtTimeStamp((timeStamp - pTrk->baseTime), pArgs->relTime),   // <time>
                p->latitude,                    // <lat>
                p->longitude,                   // <lon>
                p->elevation);                  // <ele>
        fprintf(pArgs->outFile, "%d,%d,%d,%d,%.10lf,%.3lf,%.10lf,%.10lf,%.10lf,%.10lf,%.2lf,%.2lf\n",
                p->power,                       // <power>
                p->ambTemp,                     // <atemp>
                p->cadence,                     // <cadence>
                p->heartRate,                   // <hr>
                p->deltaT,                      // <deltaT>
                p->run,                         // <run>
                p->rise,                        // <rise>
                p->dist,                        // <dist>
                mToKm(p->distance),             // <distance> [km]
                mpsToKph(p->speed),             // <speed> [km/h]
                p->grade,                       // <grade> [%]
                (prev != NULL) ? fabs(p->grade - prev->grade) : 0);
    }
}

static int gpxActType(GpsTrk *pTrk, CmdArgs *pArgs)
{
    int type;

    if (pArgs->actType != undef) {
        type = pArgs->actType;
    } else if (pTrk->type != 0) {
        type = pTrk->type;
    } else {
        type = 1;   // default: Ride
    }

    return type;
}

static void printGpxFmt(GpsTrk *pTrk, CmdArgs *pArgs)
{
    time_t now;
    struct tm brkDwnTime = {0};
    char timeBuf[128];
    TrkPt *p;

    // Print headers
    fprintf(pArgs->outFile, "%s", xmlHeader);
    fprintf(pArgs->outFile, gpxHeader, progVerMajor, progVerMinor);

    // Print metadata
    now = time(NULL);
    strftime(timeBuf, sizeof (timeBuf), "%Y-%m-%dT%H:%M:%S", gmtime_r(&now, &brkDwnTime));
    fprintf(pArgs->outFile, "  <metadata>\n");
    fprintf(pArgs->outFile, "    <name> %s </name>\n", pArgs->name);
    fprintf(pArgs->outFile, "    <author>gpxFileTool version %d.%d [https://github.com/elfrances/gpxFileTool.git]</author>\n", progVerMajor, progVerMinor);
    fprintf(pArgs->outFile, "    <desc> ");
    for (int n = 1; n < pArgs->argc; n++) {
        fprintf(pArgs->outFile, "%s ", pArgs->argv[n]);
    }
    fprintf(pArgs->outFile, "    </desc>\n");
    fprintf(pArgs->outFile, "    <time>%s</time>\n", timeBuf);
    fprintf(pArgs->outFile, "  </metadata>\n");

    // Print track
    fprintf(pArgs->outFile, "  <trk>\n");
    if (pArgs->name != NULL) {
        fprintf(pArgs->outFile, "    <name>%s</name>\n", pArgs->name);
    }
    fprintf(pArgs->outFile, "    <type>%d</type>\n", gpxActType(pTrk, pArgs));

    // Print track segment
    fprintf(pArgs->outFile, "    <trkseg>\n");

    // Print all the track points
    TAILQ_FOREACH(p, &pTrk->trkPtList, tqEntry) {
        double timeStamp = (p->adjTime != 0.0) ? p->adjTime : p->timestamp;    // use the adjusted timestamp if there is one
        time_t time;
        int ms = 0;

        // Account for the time shift due to SMA
        if ((pArgs->smaWindow != 0) && (p->index < pArgs->smaWindow))
            continue;

        timeStamp += pTrk->timeOffset;
        time = (time_t) timeStamp;  // sec only
        ms = (timeStamp - (double) time) * 1000.0;  // milliseconds
        strftime(timeBuf, sizeof (timeBuf), "%Y-%m-%dT%H:%M:%S", gmtime_r(&time, &brkDwnTime));
        fprintf(pArgs->outFile, "      <trkpt lat=\"%.10lf\" lon=\"%.10lf\">\n", p->latitude, p->longitude);
        fprintf(pArgs->outFile, "        <ele>%.10lf</ele>\n", p->elevation);
        fprintf(pArgs->outFile, "        <time>%s.%03dZ</time>\n", timeBuf, ms);
        if (pArgs->outMask != SD_NONE) {
            fprintf(pArgs->outFile, "        <extensions>\n");
            if ((pTrk->inMask & SD_POWER) && (pArgs->outMask & SD_POWER)) {
                fprintf(pArgs->outFile, "          <power>%d</power>\n", p->power);
            }
            if ((pTrk->inMask & (SD_ATEMP | SD_CADENCE | SD_HR)) && (pArgs->outMask & (SD_ATEMP | SD_CADENCE | SD_HR))) {
                fprintf(pArgs->outFile, "          <gpxtpx:TrackPointExtension>\n");
                if ((pTrk->inMask & SD_ATEMP) && (pArgs->outMask & SD_ATEMP)) {
                    fprintf(pArgs->outFile, "            <gpxtpx:atemp>%d</gpxtpx:atemp>\n", p->ambTemp);
                }
                if ((pTrk->inMask & SD_HR) && (pArgs->outMask & SD_HR)) {
                    fprintf(pArgs->outFile, "            <gpxtpx:hr>%d</gpxtpx:hr>\n", p->heartRate);
                }
                if ((pTrk->inMask & SD_CADENCE) && (pArgs->outMask & SD_CADENCE)) {
                    fprintf(pArgs->outFile, "            <gpxtpx:cad>%d</gpxtpx:cad>\n", p->cadence);
                }
                fprintf(pArgs->outFile, "          </gpxtpx:TrackPointExtension>\n");
            }
            fprintf(pArgs->outFile, "        </extensions>\n");
        }

        fprintf(pArgs->outFile, "      </trkpt>\n");
    }

    fprintf(pArgs->outFile, "    </trkseg>\n");

    fprintf(pArgs->outFile, "  </trk>\n");

    fprintf(pArgs->outFile, "</gpx>\n");
}

static const char *tcxActType(GpsTrk *pTrk, CmdArgs *pArgs)
{
    int type;
    static const char *actTypeTbl[] = {
            [undef]     "???",
            [ride]      "Biking",
            [hike]      "Hiking",
            [run]       "Running",
            [walk]      "Walking",
            [vride]     "Virtual Cycling",
            [other]     "Other"
    };

    if (pArgs->actType != undef) {
        type = pArgs->actType;
    } else if (pTrk->type != 0) {
        type = pTrk->type;
    } else {
        type = 1;   // default: Ride
    }

    return actTypeTbl[type];
}

// Format the data according to the FulGaz ".shiz" format
static void printShizFmt(GpsTrk *pTrk, CmdArgs *pArgs)
{
    time_t now;
    struct tm brkDwnTime = {0};
    char dateBuf[64];
    double baseTime = TAILQ_FIRST(&pTrk->trkPtList)->timestamp;
    TrkPt *p;

    now = time(NULL);
    strftime(dateBuf, sizeof (dateBuf), "%A, %B %d, %Y", gmtime_r(&now, &brkDwnTime));

    // This format is valid as of FulGaz version 4.2.15
    // Duration is in hh:mm:ss, distance is in kilometers,
    // elevation is in meters, and speed is in km/h.
    fprintf(pArgs->outFile, "{\"extra\":{\"duration\":\"%s\",\"distance\":%.5lf,\"toughness\":\"%u\",\"elevation_gain\":%u,\"date_processed\":\"%s\",\"speed_filter\":\"5\",\"elevation_filter\":\"4\",\"grade_filter\":\"4\",\"timeshift\":\"-4\"},\"gpx\":{\"trk\":{\"trkseg\":{\"trkpt\":[",
            fmtTimeStamp(pTrk->time, hms), mToKm(pTrk->distance), 123, (unsigned) pTrk->elevGain, dateBuf);

    TAILQ_FOREACH(p, &pTrk->trkPtList, tqEntry) {
        // The first "trkpt" is included in the header line,
        // while all the other ones are printed on separate
        // lines...

        // Account for the time shift due to SMA
        if ((pArgs->smaWindow != 0) && (p->index < pArgs->smaWindow))
            continue;

        fprintf(pArgs->outFile, "{\"-lon\":\"%.7lf\",\"-lat\":\"%.7lf\",\"speed\":\"%.1lf\",\"ele\":\"%.3lf\",\"distance\":\"%.5lf\",\"bearing\":\"%.2lf\",\"slope\":\"%.1lf\",\"time\":\"%s\",\"index\":%u,\"cadence\":%u,\"p\":%u}%s",
                p->longitude, p->latitude, mpsToKph(p->speed), p->elevation, mToKm(p->distance), p->bearing, p->grade, fmtTimeStamp((p->timestamp - baseTime), hms), p->index, p->cadence, 0, (TAILQ_NEXT(p, tqEntry) != NULL) ? ",\n" : "");
    }

    fprintf(pArgs->outFile, "]}},\"seg\":[]}}\n");
}

// Format the data according to the Garmin Connect style
static void printTcxFmt(GpsTrk *pTrk, CmdArgs *pArgs)
{
    time_t now;
    struct tm brkDwnTime = {0};
    char timeBuf[128];
    TrkPt *p;

    // Print headers
    fprintf(pArgs->outFile, "%s", xmlHeader);
    fprintf(pArgs->outFile, "%s", tcxHeader);

    // Print metadata
    now = time(NULL);
    strftime(timeBuf, sizeof (timeBuf), "%Y-%m-%dT%H:%M:%S", gmtime_r(&now, &brkDwnTime));

    fprintf(pArgs->outFile, "  <Activities>\n");
    fprintf(pArgs->outFile, "    <Activity Sport=\"%s\">\n", tcxActType(pTrk, pArgs));
    fprintf(pArgs->outFile, "      <Id>%s</Id>\n", timeBuf);
    fprintf(pArgs->outFile, "      <Lap StartTime=\"%s\">\n", timeBuf);
    fprintf(pArgs->outFile, "        <TotalTimeSeconds>%.3lf</TotalTimeSeconds>\n", pTrk->time);
    fprintf(pArgs->outFile, "        <DistanceMeters>%.10lf</DistanceMeters>\n", pTrk->distance);
    fprintf(pArgs->outFile, "        <MaximumSpeed>%.10lf</MaximumSpeed>\n", pTrk->maxSpeed);
    fprintf(pArgs->outFile, "        <AverageHeartRateBpm>\n");
    fprintf(pArgs->outFile, "          <Value>%d</Value>\n", (pTrk->heartRate / pTrk->numTrkPts));
    fprintf(pArgs->outFile, "        </AverageHeartRateBpm>\n");
    fprintf(pArgs->outFile, "        <MaximumHeartRateBpm>\n");
    fprintf(pArgs->outFile, "          <Value>%d</Value>\n", pTrk->maxHeartRate);
    fprintf(pArgs->outFile, "        </MaximumHeartRateBpm>\n");
    fprintf(pArgs->outFile, "        <Cadence>%d</Cadence>\n", pTrk->maxCadence);   // this <Cadence> seems to be the max cadence value
    fprintf(pArgs->outFile, "        <TriggerMethod>Manual</TriggerMethod>\n");
    fprintf(pArgs->outFile, "        <Track>\n");

    // Print all the track points
    TAILQ_FOREACH(p, &pTrk->trkPtList, tqEntry) {
        double timeStamp = (p->adjTime != 0.0) ? p->adjTime : p->timestamp;    // use the adjusted timestamp if there is one
        time_t time;
        int ms = 0;

        // Account for the time shift due to SMA
        if ((pArgs->smaWindow != 0) && (p->index < pArgs->smaWindow))
            continue;

        timeStamp += pTrk->timeOffset;
        time = (time_t) timeStamp;  // sec only
        ms = (timeStamp - (double) time) * 1000.0;  // milliseconds
        strftime(timeBuf, sizeof (timeBuf), "%Y-%m-%dT%H:%M:%S", gmtime_r(&time, &brkDwnTime));

        fprintf(pArgs->outFile, "          <Trackpoint>\n");
        fprintf(pArgs->outFile, "            <Time>%s.%03dZ</Time>\n", timeBuf, ms);
        fprintf(pArgs->outFile, "            <Position>\n");
        fprintf(pArgs->outFile, "              <LatitudeDegrees>%.10lf</LatitudeDegrees>\n", p->latitude);
        fprintf(pArgs->outFile, "              <LongitudeDegrees>%.10lf</LongitudeDegrees>\n", p->longitude);
        fprintf(pArgs->outFile, "            </Position>\n");
        fprintf(pArgs->outFile, "            <AltitudeMeters>%.10lf</AltitudeMeters>\n", p->elevation);
        fprintf(pArgs->outFile, "            <DistanceMeters>%.10lf</DistanceMeters>\n", p->distance);
        if ((pTrk->inMask & SD_HR) && (pArgs->outMask & SD_HR)) {
            fprintf(pArgs->outFile, "            <HeartRateBpm>\n");
            fprintf(pArgs->outFile, "              <Value>%d</Value>\n", p->heartRate);
            fprintf(pArgs->outFile, "            </HeartRateBpm>\n");
        }
        if ((pTrk->inMask & SD_CADENCE) && (pArgs->outMask & SD_CADENCE)) {
            fprintf(pArgs->outFile, "            <Cadence>%d</Cadence>\n", p->cadence);
        }
        fprintf(pArgs->outFile, "            <Extensions>\n");
        fprintf(pArgs->outFile, "              <ns3:TPX>\n");
        fprintf(pArgs->outFile, "                <ns3:Speed>%.10lf</ns3:Speed>\n", p->speed);
        if ((pTrk->inMask & SD_POWER) && (pArgs->outMask & SD_POWER)) {
            fprintf(pArgs->outFile, "                <ns3:Watts>%d</ns3:Watts>\n", p->power);
        }
        fprintf(pArgs->outFile, "              </ns3:TPX>\n");
        fprintf(pArgs->outFile, "            </Extensions>\n");
        fprintf(pArgs->outFile, "          </Trackpoint>\n");
    }

    fprintf(pArgs->outFile, "        </Track>\n");
    fprintf(pArgs->outFile, "      </Lap>\n");
    fprintf(pArgs->outFile, "    </Activity>\n");
    fprintf(pArgs->outFile, "  </Activities>\n");
    fprintf(pArgs->outFile, "  <Author xsi:type=\"Application_t\">\n");
    fprintf(pArgs->outFile, "    <Name>gpxFileTool https://github.com/elfrances/gpxFileTool.git</Name>\n");
    fprintf(pArgs->outFile, "    <Build>\n");
    fprintf(pArgs->outFile, "      <Version>\n");
    fprintf(pArgs->outFile, "        <VersionMajor>%d</VersionMajor>\n", progVerMajor);
    fprintf(pArgs->outFile, "        <VersionMinor>%d</VersionMinor>\n", progVerMinor);
    fprintf(pArgs->outFile, "      </Version>\n");
    fprintf(pArgs->outFile, "    </Build>\n");
    fprintf(pArgs->outFile, "    <LangID>en</LangID>\n");
    fprintf(pArgs->outFile, "  </Author>\n");
    fprintf(pArgs->outFile, "</TrainingCenterDatabase>\n");
}

static void printOutput(GpsTrk *pTrk, CmdArgs *pArgs)
{
    if (pArgs->summary) {
        printSummary(pTrk, pArgs);
    } else if (pArgs->outFmt == csv) {
        printCsvFmt(pTrk, pArgs);
    } else if (pArgs->outFmt == gpx) {
        printGpxFmt(pTrk, pArgs);
    } else if (pArgs->outFmt == shiz) {
        printShizFmt(pTrk, pArgs);
    } else if (pArgs->outFmt == tcx) {
        printTcxFmt(pTrk, pArgs);
    }
}

int main(int argc, char **argv)
{
    CmdArgs cmdArgs = {0};
    GpsTrk gpsTrk = {0};
    TrkPt *pTrkPt;
    int n;

    // Parse the command arguments
    if ((n = parseArgs(argc, argv, &cmdArgs)) < 0) {
        return -1;
    }

    TAILQ_INIT(&gpsTrk.trkPtList);

    // Process each GPX/TCX input file
    while (n < argc) {
        const char *fileSuffix;
        int s;
        cmdArgs.inFile = argv[n++];
        if ((fileSuffix = strrchr(cmdArgs.inFile, '.')) == NULL) {
            fprintf(stderr, "Unsupported input file %s\n", cmdArgs.inFile);
            return -1;
        }
        if (strcmp(fileSuffix, ".gpx") == 0) {
            s = parseGpxFile(&cmdArgs, &gpsTrk);
        } else if (strcmp(fileSuffix, ".tcx") == 0) {
            s = parseTcxFile(&cmdArgs, &gpsTrk);
        } else {
            fprintf(stderr, "Unsupported input file %s\n", cmdArgs.inFile);
            return -1;
        }
        if (s != 0) {
            fprintf(stderr, "Failed to parse input file %s\n", cmdArgs.inFile);
            return -1;
        }
        cmdArgs.inFile = NULL;
    }

    // Done parsing all the input files. Make sure we have
    // at least one TrkPt!
    if ((pTrkPt = TAILQ_FIRST(&gpsTrk.trkPtList)) == NULL) {
        // Hu?
        fprintf(stderr, "No track points found!\n");
        return -1;
    }

    // The first point is used as the reference point, so we
    // must check a few things before we proceed...

    if (pTrkPt->elevation == nilElev) {
        // If the first TrkPt is missing its elevation data,
        // as is the case with some GPX/TCX files exported by
        // some tools, the grade value of the second TrkPt
        // will be huge...
        fprintf(stderr, "ERROR: TrkPt #%d (%s) is missing its elevation data !\n",
                pTrkPt->index, fmtTrkPtIdx(pTrkPt));
        return -1;
    }

    if (pTrkPt->timestamp == 0.0) {
        // TrkPt has no time information, likely because this
        // is a GPX/TCX route, and not an actual GPX/TCX ride.
        // In this case we need to have a start time and a set
        // speed defined, in order to be able to calculate the
        // timestamps that turn the route into a ride.
        if ((cmdArgs.startTime == 0) || (cmdArgs.setSpeed == 0.0)) {
            fprintf(stderr, "TrkPt #%d (%s) is missing time information and no startTime or setSpeed has been specified to turn a route into an activity!\n",
                    pTrkPt->index, fmtTrkPtIdx(pTrkPt));
            return -1;
        }

        // Set the timestamp of the first point to the desired
        // start time of the ride (activity).
        pTrkPt->timestamp = cmdArgs.startTime;
    } else if (cmdArgs.startTime != 0.0) {
        // We are changing the start date/time of the activity
        // so set the time offset used to adjust the timestamp
        // of each point accordingly.
        gpsTrk.timeOffset = cmdArgs.startTime - pTrkPt->timestamp;
    }

    // Now run some consistency checks on all the TrkPt's
    if (checkTrkPts(&gpsTrk, &cmdArgs) != 0) {
        fprintf(stderr, "Failed to trim/delete TrkPt's\n");
        return -1;
    }

    // Set the activity's start time
    gpsTrk.startTime = pTrkPt->timestamp;

    // If necessary, set the base time reference used to
    // generate relative timestamps in the CSV output data.
    if (cmdArgs.relTime) {
        gpsTrk.baseTime = pTrkPt->timestamp;
    }

    // At this point gpsTrk.trkPtList contains all the track
    // points from all the GPX/TCX input files...

    if (cmdArgs.closeGap) {
        // Close the time gap at the specified track
        // point.
        closeTimeGap(&gpsTrk, &cmdArgs);
    }

    // Compute the speed & grade data
    if (compDataPhase1(&gpsTrk, &cmdArgs) != 0) {
        fprintf(stderr, "Failed to compute speed/grade!\n");
        return -1;
    }

    // Do necessary adjustments
    compDataPhase2(&gpsTrk, &cmdArgs);

    // Compute min/max values
    compDataPhase3(&gpsTrk, &cmdArgs);

    // Generate the output data
    printOutput(&gpsTrk, &cmdArgs);

    if (cmdArgs.outFile != stdout) {
        fclose(cmdArgs.outFile);
    }

    return 0;
}


