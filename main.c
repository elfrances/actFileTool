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
 *   the "run" is not a straight line, but rather the great-circle distance
 *   over the Earth's surface. But when the two points are "close together",
 *   as during a slow speed activity like cycling, when the sample points
 *   are spaced apart by just 1 second, we can assume the run is a straight
 *   line, and hence we are dealing with a rectangular triangle.
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
 *   Assuming the angle at P1, between "dist" and "run", is "theta", then
 *   the following equations describe the relationship between the various
 *   values:
 *
 *   slope = rise / run = tan(theta)
 *
 *   dist^2 = run^2 + rise^2
 *
 *   dist = speed * (t2 - t1)
 *
 *   The "rise" is simply the elevation (altitude) difference between
 *   the two points. In a consumer-level GPS device, the error in the
 *   elevation value can be 3X the error in the latitude/longitude
 *   values. The following article has useful info about the elevation
 *   measurement:
 *
 *   https://eos-gnss.com/knowledge-base/articles/elevation-for-beginners
 *
 *   Having an actual speed sensor on the bike during a cycling activity is
 *   helpful because that way "dist" can be easily an accurately computed
 *   from the speed value and the time difference, which is typically fixed
 *   at 1 second. If no speed sensor is available, the "dist" value needs
 *   to be computed from the "run" and "rise" values, using Pythagoras's
 *   Theorem, where the "run" value is computed from the latitude/longitude
 *   using the Haversine formula.
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
#include "input.h"
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
        "    --csv-time-format {hms|sec|utc}\n"
        "        Specifies the format of the timestamp value in the CSV output.\n"
        "        'hms' and 'sec' imply relative timestamps, while 'utc' implies\n"
        "        absolute timestamps.\n"
        "    --csv-units {imperial|metric}\n"
        "        Specifies the type of units to use in the CSV output.\n"
        "    --help\n"
        "        Show this help and exit.\n"
        "    --max-grade <value>\n"
        "        Limit the maximum grade to the specified value. The elevation\n"
        "        values are adjusted accordingly.\n"
        "    --max-grade-change <value>\n"
        "        Limit the maximum change in grade between points to the specified\n"
        "        value. The elevation values are adjusted accordingly.\n"
        "    --max-speed-change <value>\n"
        "        Limit the maximum change in speed between points to the specified\n"
        "        value.\n"
        "    --min-grade <value>\n"
        "        Limit the minimum grade to the specified value. The elevation\n"
        "        values are adjusted accordingly.\n"
        "    --name <name>\n"
        "        String to use for the <name> tag of the track in the output\n"
        "        file.\n"
        "    --no-elev-adj\n"
        "        Do not auto-adjust the elevation values when the grade values are\n"
        "        modified.\n"
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
        "    --set-speed <avg-speed>\n"
        "        Use the specified average speed value (in km/h) to generate missing\n"
        "        timestamps, or to replace the existing timestamps, in the input file.\n"
        "    --start-time <time>\n"
        "        Start time for the activity (in UTC time). The timestamp of each\n"
        "        point is adjusted accordingly. Format is: 2018-01-22T10:01:10Z.\n"
        "    --summary\n"
        "        Print only a summary of the activity metrics in human-readable\n"
        "        form and exit.\n"
        "    --trim <a,b>\n"
        "        Trim all the points in the specified range. The timestamps of\n"
        "        the points after point 'b' are adjusted accordingly, to avoid\n"
        "        a discontinuity in the time sequence. If point 'a' happens to be\n"
        "        the first point in the track, then the start time of the activity\n"
        "        is adjusted as well.\n"
        "    --verbatim\n"
        "        Process the input file(s) verbatim, without making any adjust-\n"
        "        ments to the data.\n"
        "    --version\n"
        "        Show version information and exit.\n"
        "    --xma-method {simple|weighed}\n"
        "        Specifies the type of Moving Average to compute: SMA or WMA.\n"
        "    --xma-metric {elevation|grade|power|speed}\n"
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

    // By default no max/min grade limits
    pArgs->maxGrade = nilGrade;
    pArgs->minGrade = nilGrade;

    // By default display metric units
    pArgs->units = metric;

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
        } else if (strcmp(arg, "--csv-time-format") == 0) {
            val = argv[++n];
            if (strcmp(val, "hms") == 0) {
                pArgs->tsFmt = hms;
            } else if (strcmp(val, "sec") == 0) {
                pArgs->tsFmt = sec;
            } else if (strcmp(val, "utc") == 0) {
                pArgs->tsFmt = utc;
            } else {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--csv-units") == 0) {
            val = argv[++n];
            if (strcmp(val, "imperial") == 0) {
                pArgs->units = imperial;
            } else if (strcmp(val, "metric") == 0) {
                pArgs->units = metric;
            } else {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--max-grade") == 0) {
            val = argv[++n];
            if ((sscanf(val, "%le", &pArgs->maxGrade) != 1) ||
                (pArgs->maxGrade < -99.9) ||
                (pArgs->maxGrade > 99.9)) {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--max-grade-change") == 0) {
            val = argv[++n];
            if ((sscanf(val, "%le", &pArgs->maxGradeChange) != 1) ||
                (pArgs->maxGradeChange < 0.1) ||
                (pArgs->maxGradeChange > 999.9)) {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--max-speed-change") == 0) {
            val = argv[++n];
            if ((sscanf(val, "%le", &pArgs->maxSpeedChange) != 1) ||
                (pArgs->maxSpeedChange < 0.1) ||
                (pArgs->maxSpeedChange > 999.9)) {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--min-grade") == 0) {
            val = argv[++n];
            if ((sscanf(val, "%le", &pArgs->minGrade) != 1) ||
                (pArgs->minGrade < -99.9) ||
                (pArgs->minGrade > 99.9)) {
                invalidArgument(arg, val);
                return -1;
            }
        } else if (strcmp(arg, "--name") == 0) {
            val = argv[++n];
            if ((pArgs->name = strdup(val)) == NULL) {
                fprintf(stderr, "Can't copy name argument: %s\n", val);
                return -1;
            }
        }  else if (strcmp(arg, "--no-elev-adj") == 0) {
            pArgs->noElevAdj = true;
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
        } else if (strcmp(arg, "--trim") == 0) {
            val = argv[++n];
            if (sscanf(val, "%d,%d", &pArgs->trimFrom, &pArgs->trimTo) != 2) {
                invalidArgument(arg, val);
                return -1;
            }
            if ((pArgs->trimFrom < 1) || (pArgs->trimFrom > pArgs->trimTo)) {
                fprintf(stderr, "Invalid TrkPt range %d,%d\n", pArgs->trimFrom, pArgs->trimTo);
                return -1;
            }
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
            } else if (strcmp(val, "speed") == 0) {
                pArgs->xmaMetric = speed;
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

static TrkPt *trimTrkPts(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p = TAILQ_FIRST(&pTrk->trkPtList);
    Bool discTrkPt = false;
    Bool trimTrkPts = false;
    double trimmedTime = 0.0;
    double trimmedDistance = 0.0;
    TrkPt *p0 = NULL;

    // Discard any points in the specified trim range
    while (p != NULL) {
        discTrkPt = false;

        // Do we need to trim out this TrkPt?
        if (p->index == pArgs->trimFrom) {
            // Start trimming
            if (!pArgs->quiet) {
                fprintf(stderr, "INFO: start trimming at TrkPt #%d (%s)\n", p->index, fmtTrkPtIdx(p));
            }
            trimTrkPts = true;
            pTrk->numTrimTrkPts++;
            discTrkPt = true;
            p0 = p; // set baseline
        } else if (p->index == pArgs->trimTo) {
            // Stop trimming
            if (!pArgs->quiet) {
                fprintf(stderr, "INFO: stop trimming at TrkPt #%d (%s)\n", p->index, fmtTrkPtIdx(p));
            }
            trimTrkPts = false;
            trimmedTime = p->timestamp - p0->timestamp;     // total time trimmed out
            trimmedDistance = p->distance - p0->distance;   // total distance trimmed out
            pTrk->numTrimTrkPts++;
            discTrkPt = true;
        } else if (trimTrkPts) {
            // Trim this point
            pTrk->numTrimTrkPts++;
            discTrkPt = true;
        }

        // Discard?
        if (discTrkPt) {
            // Remove this TrkPt from the list
            p = remTrkPt(pTrk, p);
        } else {
            // If we trimmed out some previous TrkPt's, then we
            // need to adjust the timestamp and distance values
            // of this TrkPt so as to "close the gap".
            if (p0 != NULL) {
                p->timestamp -= trimmedTime;
                p->distance -= trimmedDistance;
            }
            p = nxtTrkPt(NULL, p);
        }
    }

    return TAILQ_FIRST(&pTrk->trkPtList);
}

static int checkTrkPts(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt
    Bool discTrkPt = false;
    double trimmedTime = 0.0;
    double trimmedDistance = 0.0;
    TrkPt *p0 = NULL;

    while (p2 != NULL) {
        // Discard any duplicate points...
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

static double xmaGetVal(const TrkPt *p, XmaMetric xmaMetric)
{
    if (xmaMetric == elevation) {
        return (double) p->elevation;
    } else if (xmaMetric == grade) {
        return (double) p->grade;
    } else if (xmaMetric == power) {
        return (double) p->power;
    } else {
        return p->speed;
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
    } else if (xmaMetric == power) {
        oldVal = p->power;
        p->power = (int) value;
    } else {
        oldVal = p->speed;
        p->speed = value;
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
    Bool valAdj;

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
    valAdj = xmaSetVal(p, xmaMetric, xmaVal);

    if (valAdj && (xmaMetric == grade)) {
        // Flag that this point had its grade adjusted
        p->adjGrade = true;
    }
}

static int smoothMetric(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt

    while (p2 != NULL) {
        if (pointWithinRange(pArgs, p2)) {
            compMovAvg(pTrk, p2, pArgs->xmaMethod, pArgs->xmaMetric, pArgs->xmaWindow);
        }

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
}

// Compute the great-circle distance (in meters) between two
// track points using the Haversine formula. See below for
// the details:
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

static int compMetrics(GpsTrk *pTrk, CmdArgs *pArgs)
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

        // FIT/TCX files include the "distance" metric which
        // is the distance (in meters) from the start up to
        // the given point. For GPX files, we need to compute
        // the distance between consecutive points using the
        // GPS data.
        if (p2->distance != 0.0) {
            if ((p2->dist = p2->distance - p1->distance) == 0.0) {
                // Stopped?
                if (!pArgs->verbatim) {
                    if (!pArgs->quiet) {
                        fprintf(stderr, "WARNING: TrkPt #%d (%s) has a null distance value !\n",
                                p2->index, fmtTrkPtIdx(p2));
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
                    fprintf(stderr, "WARNING: TrkPt #%d (%s) has inconsistent dist=%.3lf and rise=%.3lf values !\n",
                            p2->index, fmtTrkPtIdx(p2), p2->dist, absRise);
                    printTrkPt(p2);
                }
                p2->run = p2->dist; // assume a null grade
            }
        } else {
            // Compute the horizontal distance "run" between
            // the two points, based on their latitude and
            // longitude values.
            if ((p2->run = compDistance(p1, p2)) == 0.0) {
                // Stopped?
                if (!pArgs->verbatim) {
                    if (!pArgs->quiet) {
                        fprintf(stderr, "WARNING: TrkPt #%d (%s) has a null run value !\n",
                                p2->index, fmtTrkPtIdx(p2));
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

        if (p2->speed == nilSpeed) {
            // Compute the speed as "distance over time"
            p2->speed = p2->dist / p2->deltaT;
            if (p2->speed > 27.78) {
                fprintf(stderr, "SPONG! TrkPt #%u (%s) has a bogus speed value ! dist=%.10lf deltaT=%.3lf speed=%.3lf\n",
                		 p2->index, fmtTrkPtIdx(p2), p2->dist, p2->deltaT, p2->speed);
            }
        }

        // Update the total distance for the activity
        pTrk->distance += p2->dist;

        // Update the total time for the activity
        pTrk->time += p2->deltaT;

        if (p2->grade == nilGrade) {
            // Compute the grade as "rise over run". Notice
            // that the grade value may get updated later.
            // Guard against points with run=0, which can
            // happen when using the "--verbose" option...
            if (p2->run != 0.0) {
                p2->grade = (p2->rise * 100.0) / p2->run;   // in [%]
            } else {
                if (!pArgs->quiet) {
                    fprintf(stderr, "WARNING: TrkPt #%d (%s) has a null run value !\n",
                            p2->index, fmtTrkPtIdx(p2));
                }
                p2->grade = p1->grade;  // carry over the previous grade value
            }
        }

        // Sanity check the grade value
        if (p2->grade > 99.9) {
            p2->grade = 99.9;
        } else if (p2->grade < -99.9) {
            p2->grade = -99.9;
        }

        // Compute the bearing
        p2->bearing = compBearing(p1, p2);

        // Compute the grade change
        p2->deltaG = fabs(p2->grade - p1->grade);

        // Update the activity's end time
        pTrk->endTime = p2->timestamp;

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
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
                p2->index, fmtTrkPtIdx(p2), p2->deltaG, pArgs->maxGradeChange);
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

#if 0
static void adjSpeedChange(GpsTrk *pTrk, CmdArgs *pArgs, TrkPt *p1, TrkPt *p2)
{
    double maxSpeedChange = (p1->speed * pArgs->maxSpeedChange) / 100.0;

    if (!pArgs->quiet) {
        fprintf(stderr, "WARNING: TrkPt #%d (%s) has a speed change of %.2lf%% that is above the limit %.2lf%% !\n",
                p2->index, fmtTrkPtIdx(p2), p2->deltaS, pArgs->maxSpeedChange);
    }

    // Override original value with the max value
    if (p2->speed > p1->speed) {
        p2->speed = p1->speed + maxSpeedChange;
    } else {
        p2->speed = p1->speed - maxSpeedChange;
    }
}
#endif

#if 0
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
        //fprintf(stderr, "%s: index=%d before=%.3lf after=%.3lf\n", __func__, p2->index, p2->elevation, adjElev);
        p2->elevation = adjElev;
        pTrk->numElevAdj++;
    }
}
#else
// Given a fixed "run" and a desired grade value, figure
// out the "rise", and adjust the elevation and "dist"
// values as needed.
//
//   rise = run * grade;
//   dist = sqrt(run^2 + rise^2);
//
static void adjElevation(GpsTrk *pTrk, TrkPt *p1, TrkPt *p2)
{
    double run = p2->run;
    double rise = run * (p2->grade / 100.0);
    double dist = sqrt((run * run) + (rise * rise));
    double adjElev;

    adjElev = p1->elevation + rise;
    if (adjElev != p2->elevation) {
        //fprintf(stderr, "%s: index=%d before=%.3lf after=%.3lf\n", __func__, p2->index, p2->elevation, adjElev);
        p2->rise = rise;
        p2->dist = dist;
        p2->elevation = adjElev;
        //if (p2->deltaT != 0.0) {
        //    p2->speed = (p2->dist / p2->deltaT);
        //}
        pTrk->numElevAdj++;
    }
}
#endif

static int limitGrade(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt

    while (p2 != NULL) {
        // The following adjustments are done regardless
        // of the --verbatim option, but only to the set
        // of points in the specified range...
        if (pointWithinRange(pArgs, p2)) {
            // See if we need to limit the max grade values
            if ((pArgs->maxGrade != nilGrade) && (p2->grade > pArgs->maxGrade)) {
                adjMaxGrade(pTrk, pArgs, p1, p2);
            }

            // See if we need to limit the min grade values
            if ((pArgs->minGrade != nilGrade) && (p2->grade < pArgs->minGrade)) {
                adjMinGrade(pTrk, pArgs, p1, p2);
            }

            // See if we need to limit the max grade change
            if ((pArgs->maxGradeChange != 0.0) && (p2->deltaG > pArgs->maxGradeChange)) {
                adjGradeChange(pTrk, pArgs, p1, p2);
            }
        }

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
}

static int adjElev(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt

    while (p2 != NULL) {
        // The following adjustments are done regardless
        // of the --verbatim option, but only to the set
        // of points in the specified range...
        if (pointWithinRange(pArgs, p2)) {
            if (p2->adjGrade) {
                adjElevation(pTrk, p1, p2);
            }
        }

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
}

#if 0
static int compDataPhase2(GpsTrk *pTrk, CmdArgs *pArgs)
{
    TrkPt *p1 = TAILQ_FIRST(&pTrk->trkPtList);  // previous TrkPt
    TrkPt *p2 = TAILQ_NEXT(p1, tqEntry);    // current TrkPt

    while (p2 != NULL) {
        // The following adjustments are done regardless
        // of the --verbatim option, but only to the set
        // of points in the specified range...
        if (pointWithinRange(pArgs, p2)) {
            // Do we need to smooth out any values, other
            // than elevation (which we already did) ?
            if ((pArgs->xmaWindow != 0) && (pArgs->xmaMetric != elevation)) {
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

            // See if we need to limit the max grade change
            p2->deltaG = fabs(p2->grade - p1->grade);
            if ((pArgs->maxGradeChange != 0.0) && (p2->deltaG > pArgs->maxGradeChange)) {
                adjGradeChange(pTrk, pArgs, p1, p2);
            }

            // Update the max grade change
            if (p2->deltaG > pTrk->maxDeltaG) {
                pTrk->maxDeltaG = p2->deltaG;
                pTrk->maxDeltaGTrkPt = p2;
            }

            // See if we need to limit the max speed change
            p2->deltaS = (fabs(p2->speed - p1->speed) / fabs(p1->speed)) * 100.0;
            if ((pArgs->maxSpeedChange != 0.0) && (p2->deltaS > pArgs->maxSpeedChange)) {
                adjSpeedChange(pTrk, pArgs, p1, p2);
            }
        }

        // If necessary, correct the elevation value based
        // on the adjusted grade value.
        if (!pArgs->noElevAdj && p2->adjGrade) {
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

        p2 = nxtTrkPt(&p1, p2);
    }

    return 0;
}
#endif

static int compMinMax(GpsTrk *pTrk, CmdArgs *pArgs)
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

        // Update the max grade change
        if (p2->deltaG > pTrk->maxDeltaG) {
            pTrk->maxDeltaG = p2->deltaG;
            pTrk->maxDeltaGTrkPt = p2;
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

    // Process each FIT/GPX/TCX input file
    while (n < argc) {
        const char *fileSuffix;
        int s;
        cmdArgs.inFile = argv[n++];
        if ((fileSuffix = strrchr(cmdArgs.inFile, '.')) == NULL) {
            fprintf(stderr, "Unsupported input file %s\n", cmdArgs.inFile);
            return -1;
        }
        if (strcmp(fileSuffix, ".csv") == 0) {
            s = parseCsvFile(&cmdArgs, &gpsTrk, cmdArgs.inFile);
        } else if (strcmp(fileSuffix, ".fit") == 0) {
            s = parseFitFile(&cmdArgs, &gpsTrk, cmdArgs.inFile);
        } else if (strcmp(fileSuffix, ".gpx") == 0) {
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
        // TrkPt has no time information, likely because this is
        // a GPX/TCX route, and not an actual GPX/TCX activity.
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

    // If the user requested to trim out a range of TrkPt's
    // do it now...
    if (cmdArgs.trimFrom) {
        pTrkPt = trimTrkPts(&gpsTrk, &cmdArgs);
    }

    // Now run some consistency checks on all the TrkPt's
    if (checkTrkPts(&gpsTrk, &cmdArgs) != 0) {
        fprintf(stderr, "Failed to delete TrkPt's\n");
        return -1;
    }

    // Set the activity's start time
    gpsTrk.startTime = pTrkPt->timestamp;

    // Set the base distance reference used to generate
    // relative distance values.
    gpsTrk.baseDistance = pTrkPt->distance;

    // If necessary, set the base time reference used to
    // generate relative timestamps in the CSV output data.
    if (cmdArgs.tsFmt != utc) {
        gpsTrk.baseTime = pTrkPt->timestamp;
    }

    // At this point gpsTrk.trkPtList contains all the track
    // points from all the GPX/TCX/FIT input files...

    if (cmdArgs.closeGap) {
        // Close the time gap at the specified track
        // point.
        closeTimeGap(&gpsTrk, &cmdArgs);
    }

    // If requested, smooth out the elevation values before
    // we compute the speed and grade, so as to minimize the
    // computational errors.
    if ((cmdArgs.xmaWindow != 0) && (cmdArgs.xmaMetric == elevation)) {
        smoothMetric(&gpsTrk, &cmdArgs);
    }

    // Compute metrics
    if (compMetrics(&gpsTrk, &cmdArgs) != 0) {
        fprintf(stderr, "Failed to compute speed/grade!\n");
        return -1;
    }

    // If requested, limit the max/min grade values
    if ((cmdArgs.maxGrade != nilGrade) ||
        (cmdArgs.minGrade != nilGrade) ||
        (cmdArgs.maxGradeChange != 0)) {
        limitGrade(&gpsTrk, &cmdArgs);
    }

    // If requested, smooth out the specified metric
    if ((cmdArgs.xmaWindow != 0) && (cmdArgs.xmaMetric != elevation)) {
        smoothMetric(&gpsTrk, &cmdArgs);
    }

    // If needed, adjust the elevation values
    if (!cmdArgs.noElevAdj) {
        adjElev(&gpsTrk, &cmdArgs);
    }

    // Compute min/max values
    compMinMax(&gpsTrk, &cmdArgs);

    // Generate the output data
    printOutput(&gpsTrk, &cmdArgs);

    if (cmdArgs.outFile != stdout) {
        fclose(cmdArgs.outFile);
    }

    return 0;
}
