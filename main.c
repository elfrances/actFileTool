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
 *   Consecutive points in the track define a pseudo-triangle, where the
 *   base is the horizontal distance "run", the height is the vertical
 *   distance "rise", and the hypotenuse is the actual distance traveled
 *   between the two points. The figure is not an exact triangle, because
 *   the run is not a straight line, but the great-circle distance over
 *   the Earth's surface. But when the two points are close together, we
 *   can assume the run is a straight line, and hence we are dealing with
 *   a rectangular triangle.
 *
 *                                 + P2
 *                                /|
 *                               / |
 *                         dist /  | rise
 *                             /   |
 *                            /    |
 *                        P1 +-----+
 *                             run
 *
 *   Assuming the angle at P1, between dist and run, is "theta", then the
 *   following equations describe the relationship between the various
 *   values:
 *
 *   slope = rise / run = tan(theta)
 *
 *   dist^2 = run^2 + rise^2
 *
 *   dist = speed * (t2 - t1)
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

#include "const.h"
#include "defs.h"
#include "output.h"
#include "trkpt.h"

#ifdef _MSC_FULL_VER
// As usual, Windows/MSC has its own idiosyncrasies...
#include "win/gmtime_r.c"
#include "win/strptime.c"
#endif  // _MSC_FULL_VER

// Compile-time build info
static const char *buildInfo = "built on " __DATE__ " at " __TIME__;

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
        "    --max-grade-change <value>\n"
        "        Limit the maximum change in grade between points to the specified\n"
        "        value. The elevation values are adjusted accordingly.\n"
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
        "        Show version information and exit.\n"
        "    --xma-method {simple|weighed}\n"
        "        Specifies the type of Moving Average to compute: SMA or WMA."
        "    --xma-metric {elevation|grade|power}\n"
        "        Specifies the metric to be smoothed out by the selected Moving\n"
        "        Average method.\n"
        "    --xma-window <size>\n"
        "        Size of the window used to compute the selected Moving Average.\n"
        "        It must be an odd value.\n";

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

    // By default send output to stdout
    pArgs->outFile = stdout;

    // By default include all optional metrics in the output
    pArgs->outMask = SD_ALL;

    // By default run the SMA over the elevation value
    pArgs->xmaMethod = simple;
    pArgs->xmaMetric = elevation;

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
        } else if (strcmp(arg, "--max-grade-change") == 0) {
            val = argv[++n];
            if (sscanf(val, "%le", &pArgs->maxGradeChange) != 1) {
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
            fprintf(stdout, "Version %d.%d %s\n", PROG_VER_MAJOR, PROG_VER_MINOR, buildInfo);
            exit(0);
        } else if (strcmp(arg, "--xma-method") == 0) {
            val = argv[++n];
            if (strcmp(val, "simple") == 0) {
                pArgs->xmaMethod = simple;
            } else if (strcmp(val, "weighed") == 0) {
                pArgs->xmaMethod = weighed;
            } else {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--xma-metric") == 0) {
            val = argv[++n];
            if (strcmp(val, "elevation") == 0) {
                pArgs->xmaMetric = elevation;
            } else if (strcmp(val, "grade") == 0) {
                pArgs->xmaMetric = grade;
            } else if (strcmp(val, "power") == 0) {
                pArgs->xmaMetric = power;
            } else {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--xma-window") == 0) {
            val = argv[++n];
            if ((sscanf(val, "%d", &pArgs->xmaWindow) != 1) ||
                ((pArgs->xmaWindow % 2) == 0)) {
                invalidArgument(arg, val);
                return -1;
            }
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

    return n;
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
static int parseGpxFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile)
{
    FILE *fp;
    TrkPt *pTrkPt = NULL;
    int lineNum = 0;
    int metaData = 0;
    static char lineBuf[1024];
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

static int parseTcxFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile)
{
    FILE *fp;
    TrkPt *pTrkPt = NULL;
    int lineNum = 0;
    static char lineBuf[1024];
    size_t bufLen = sizeof (lineBuf);
    int trackBlock = false;

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
    lineNum = getLine(fp, lineBuf, bufLen, lineNum);
    if ((lineNum < 0) ||
        (strstr(lineBuf, "<?xml ") == NULL)) {
        fprintf(stderr, "Input file is not an XML file !!!\n");
        return -1;
    }
    lineNum = getLine(fp, lineBuf, bufLen, lineNum);
    if ((lineNum < 0) ||
        (strstr(lineBuf, "<TrainingCenterDatabase") == NULL)) {
        fprintf(stderr, "Input file is not a recognized TCX file !!!\n");
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

    // If no explicit output format has been specified,
    // use the same format as the input file.
    if (pArgs->outFmt == nil) {
        pArgs->outFmt = tcx;
    }

    fclose(fp);

    return 0;
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
                    // Carry over the data from the previous point
                    p2->bearing = p1->bearing;
                    p2->distance = p1->distance;
                    p2->grade = p1->grade;
                    p2->speed = p1->speed;

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
                if (!pArgs->quiet) {
                    fprintf(stderr, "WARNING: TrkPt #%d has inconsistent dist=%.3lf and rise=%.3lf values !\n",
                            p2->index, p2->dist, absRise);
                    printTrkPt(p2);
                }
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
                    // Carry over the data from the previous point
                    p2->bearing = p1->bearing;
                    p2->distance = p1->distance;
                    p2->grade = p1->grade;
                    p2->speed = p1->speed;

                    // Move on to the next point
                    p2 = nxtTrkPt(&p1, p2);
                }
                continue;
            }

            // Compute the actual distance traveled between
            // the two points.
            if (absRise == 0.0) {
                // When riding on the flats, dist equals run!
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

static double xmaGetVal(const TrkPt *p, XmaMetric xmaMetric)
{
    if (xmaMetric == elevation) {
        return (double) p->elevation;
    } else if (xmaMetric == grade) {
        return (double) p->grade;
    } else {
        return (double) p->power;
    }
}

static Bool xmaSetVal(TrkPt *p, XmaMetric xmaMetric, double value)
{
    double oldVal;

    if (xmaMetric == elevation) {
        oldVal = p->elevation;
        p->elevation = value;
    } else if (xmaMetric == grade) {
        oldVal = p->grade;
        p->grade = value;
    } else {
        oldVal = p->power;
        p->power = (int) value;
    }

    return (value != oldVal) ? true : false;
}

// Compute the Moving Average (SMA/WMA) of the specified
// metric at the given point, using a window size of N
// points, where N is an odd value. The average is computed
// using the (N-1)/2 values before the point, the given point,
// and the (N-1)/2 values after the point.
static void compMovAvg(GpsTrk *pTrk, TrkPt *p, XmaMethod xmaMethod, XmaMetric xmaMetric, int xmaWindow)
{
    int i;
    int n = (xmaWindow - 1) / 2;    // number of points to the L/R of the given point
    int weight = 1; // SMA
    int denom = 0;
    double summ = 0.0;
    double xmaVal;
    TrkPt *tp;

    // Points before the given point
    for (i = 0, tp = TAILQ_PREV(p, TrkPtList, tqEntry); (i < n) && (tp != NULL); i++, tp = TAILQ_PREV(tp, TrkPtList, tqEntry)) {
        if (xmaMethod == weighed)
            weight = (n - i);
        summ += (xmaGetVal(tp, xmaMetric) * weight);
        denom += weight;
    }

    // The given point
    if (xmaMethod == weighed)
        weight = (n + 1);
    summ += (xmaGetVal(p, xmaMetric) * weight);
    denom += weight;

    // Points after the given point
    for (i = 0, tp = TAILQ_NEXT(p, tqEntry); (i < n) && (tp != NULL); i++, tp = TAILQ_NEXT(tp, tqEntry)) {
        if (xmaMethod == weighed)
            weight = (n - i);
        summ += (xmaGetVal(tp, xmaMetric) * weight);
        denom += weight;
    }

    // SMA/WMA value
    xmaVal = summ / denom;

    //fprintf(stderr, "%s: index=%d metric=%d before=%.3lf summ=%.3lf pts=%d after=%.3lf\n", __func__, p->index, xmaMetric, xmaGetVal(p, xmaMetric), summ, denom, xmaVal);

    // Override the original value with the
    // computed SMA/WMA value.
    if (xmaSetVal(p, xmaMetric, xmaVal)) {
        if (xmaMetric == elevation) {
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
        } else if (xmaMetric == grade) {
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

static void adjGradeChange(GpsTrk *pTrk, CmdArgs *pArgs, TrkPt *p1, TrkPt *p2)
{
    if (!pArgs->quiet) {
        fprintf(stderr, "WARNING: TrkPt #%d (%s) has a grade change of %.2lf%% that is above the limit %.2lf%% !\n",
                p2->index, fmtTrkPtIdx(p2), p2->grade, pArgs->maxGradeChange);
    }

    // Override original value with the max value
    if (p2->grade > p1->grade) {
        p2->grade = p1->grade + pArgs->maxGradeChange;
    } else {
        p2->grade = p1->grade - pArgs->maxGradeChange;
    }

    // Flag that this point had its grade adjusted
    p2->adjGrade = true;
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

static int compDataPhase2(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt
    double deltaG;

    while (p2 != NULL) {
        if (pointWithinRange(pArgs, p2)) {
            // Do we need to smooth out any values?
            if (pArgs->xmaWindow != 0) {
                compMovAvg(pTrk, p2, pArgs->xmaMethod, pArgs->xmaMetric, pArgs->xmaWindow);
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

        // See if we need to limit the max grade change
        deltaG = fabs(p2->grade - p1->grade);
        if ((pArgs->maxGradeChange != 0.0) && (deltaG > pArgs->maxGradeChange)) {
            adjGradeChange(pTrk, pArgs, p1, p2);
            deltaG = pArgs->maxGradeChange;
        }

        // If necessary, correct the elevation value based
        // on the adjusted grade value. We need to adjust
        // the "rise" value, while the "run" value remains
        // the same.
        if (p2->adjGrade) {
            adjElevation(pTrk, p1, p2);
        }

        // Update the rolling elevation gain/loss values
        if (p2->rise >= 0.0) {
            pTrk->elevGain += p2->rise;
        } else {
            pTrk->elevLoss += fabs(p2->rise);
        }

        // Update the rolling cadence, grade, heart rate,
        // power, and temp values used to compute the
        // averages for the activity.
        pTrk->cadence += p2->cadence;
        pTrk->grade += p2->grade;
        pTrk->heartRate += p2->heartRate;
        pTrk->power += p2->power;
        pTrk->temp += p2->ambTemp;

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
    pTrk->minElev = +99999.9;
    pTrk->maxElev = -99999.9;
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

        if (p2->elevation > pTrk->maxElev) {
             pTrk->maxElev = p2->elevation;
             pTrk->maxElevTrkPt = p2;
        } else if (p2->elevation < pTrk->minElev) {
            pTrk->minElev = p2->elevation;
            pTrk->minElevTrkPt = p2;
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
            s = parseGpxFile(&cmdArgs, &gpsTrk, cmdArgs.inFile);
        } else if (strcmp(fileSuffix, ".tcx") == 0) {
            s = parseTcxFile(&cmdArgs, &gpsTrk, cmdArgs.inFile);
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
