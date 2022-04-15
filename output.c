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

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "const.h"
#include "defs.h"
#include "trkpt.h"

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
        fprintf(pArgs->outFile, "    dateAndTime: %s\n", timeBuf);
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

    fprintf(pArgs->outFile, "       distance: %.3lf km\n", mToKm(pTrk->distance));
    fprintf(pArgs->outFile, "       elevGain: %.3lf m\n", pTrk->elevGain);
    fprintf(pArgs->outFile, "       elevLoss: %.3lf m\n", pTrk->elevLoss);

    // Max/Min/Avg values
    if ((p = pTrk->maxElevTrkPt) != NULL) {
        fprintf(pArgs->outFile, "        maxElev: %.3lf m @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxElev, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
    }
    if ((p = pTrk->minElevTrkPt) != NULL) {
        fprintf(pArgs->outFile, "        minElev: %.3lf m @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->minElev, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
    }

    if ((p = pTrk->maxSpeedTrkPt) != NULL) {
        fprintf(pArgs->outFile, "       maxSpeed: %.3lf km/h @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km, deltaD = %.3lf m, deltaT = %.3lf s\n",
                mpsToKph(pTrk->maxSpeed), p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance), p->dist, p->deltaT);
    }
    if ((p = pTrk->minSpeedTrkPt) != NULL) {
        fprintf(pArgs->outFile, "       minSpeed: %.3lf km/h @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km, deltaD = %.3lf m, deltaT = %.3lf s\n",
                mpsToKph(pTrk->minSpeed), p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance), p->dist, p->deltaT);
    }
    fprintf(pArgs->outFile, "       avgSpeed: %.3lf km/h\n", mpsToKph(pTrk->distance / pTrk->time));

    if ((p = pTrk->maxGradeTrkPt) != NULL) {
        fprintf(pArgs->outFile, "       maxGrade: %.2lf%% @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km, run = %.3lf m, rise = %.3lf m\n",
                pTrk->maxGrade, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance), p->run, p->rise);
    }
    if ((p = pTrk->minGradeTrkPt) != NULL) {
        fprintf(pArgs->outFile, "       minGrade: %.2lf%% @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km, run = %.3lf m, rise = %.3lf m\n",
                pTrk->minGrade, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance), p->run, p->rise);
    }
    fprintf(pArgs->outFile, "       avgGrade: %.2lf%%\n", (pTrk->grade / pTrk->numTrkPts));

    if (pTrk->inMask & SD_CADENCE) {
        p = pTrk->maxCadenceTrkPt;
        fprintf(pArgs->outFile, "     maxCadence: %d rpm @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxCadence, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        p = pTrk->minCadenceTrkPt;
        fprintf(pArgs->outFile, "     minCadence: %d rpm @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->minCadence, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        fprintf(pArgs->outFile, "     avgCadence: %d rpm\n", (pTrk->cadence / pTrk->numTrkPts));
    }
    if (pTrk->inMask & SD_HR) {
        p = pTrk->maxHeartRateTrkPt;
        fprintf(pArgs->outFile, "          maxHR: %d bpm @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxHeartRate, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        p = pTrk->minHeartRateTrkPt;
        fprintf(pArgs->outFile, "          minHR: %d bpm @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->minHeartRate, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        fprintf(pArgs->outFile, "          avgHR: %d bpm\n", (pTrk->heartRate / pTrk->numTrkPts));
    }
    if (pTrk->inMask & SD_POWER) {
        p = pTrk->maxPowerTrkPt;
        fprintf(pArgs->outFile, "       maxPower: %d watts @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxPower, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        p = pTrk->minPowerTrkPt;
        fprintf(pArgs->outFile, "       minPower: %d watts @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->minPower, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        fprintf(pArgs->outFile, "       avgPower: %d watts\n", (pTrk->power / pTrk->numTrkPts));
    }
    if (pTrk->inMask & SD_ATEMP) {
        p = pTrk->maxTempTrkPt;
        fprintf(pArgs->outFile, "        maxTemp: %d C @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->maxTemp, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
        p = pTrk->minTempTrkPt;
        fprintf(pArgs->outFile, "        minTemp: %d C @ TrkPt #%d (%s) : time = %ld s, distance = %.3lf km\n",
                pTrk->minTemp, p->index, fmtTrkPtIdx(p), (long) (p->timestamp - pTrk->baseTime), mToKm(p->distance));
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
    } else if (pTrk->actType != 0) {
        type = pTrk->actType;
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
    fprintf(pArgs->outFile, gpxHeader, PROG_VER_MAJOR, PROG_VER_MINOR);

    // Print metadata
    now = time(NULL);
    strftime(timeBuf, sizeof (timeBuf), "%Y-%m-%dT%H:%M:%S", gmtime_r(&now, &brkDwnTime));
    fprintf(pArgs->outFile, "  <metadata>\n");
    fprintf(pArgs->outFile, "    <name> %s </name>\n", pArgs->name);
    fprintf(pArgs->outFile, "    <author>gpxFileTool version %d.%d [https://github.com/elfrances/gpxFileTool.git]</author>\n", PROG_VER_MAJOR, PROG_VER_MINOR);
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
            [undef]     =   "???",
            [ride]      =   "Biking",
            [hike]      =   "Hiking",
            [run]       =   "Running",
            [walk]      =   "Walking",
            [vride]     =   "Virtual Cycling",
            [other]     =   "Other"
    };

    if (pArgs->actType != undef) {
        type = pArgs->actType;
    } else if (pTrk->actType != 0) {
        type = pTrk->actType;
    } else {
        type = 1;   // default: Ride
    }

    return actTypeTbl[type];
}

// Format the data according to the FulGaz ".shiz" format
static void printShizFmt(GpsTrk *pTrk, CmdArgs *pArgs)
{
    const int toughness = 100;
    const int speed_filter = 0;
    const int elevation_filter = 0;
    const int grade_filter = 0;
    const int timeshift = 0;
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
    fprintf(pArgs->outFile, "{\"extra\":{\"duration\":\"%s\",\"distance\":%.5lf,\"toughness\":\"%d\",\"elevation_gain\":%u,\"date_processed\":\"%s\",\"speed_filter\":\"%d\",\"elevation_filter\":\"%d\",\"grade_filter\":\"%d\",\"timeshift\":\"%d\"},\"gpx\":{\"trk\":{\"trkseg\":{\"trkpt\":[",
            fmtTimeStamp(pTrk->time, hms), mToKm(pTrk->distance), toughness, (unsigned) pTrk->elevGain, dateBuf, speed_filter, elevation_filter, grade_filter, timeshift);

    TAILQ_FOREACH(p, &pTrk->trkPtList, tqEntry) {
        // The first "trkpt" is included in the header line,
        // while all the other ones are printed on separate
        // lines...

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
    fprintf(pArgs->outFile, "        <VersionMajor>%d</VersionMajor>\n", PROG_VER_MAJOR);
    fprintf(pArgs->outFile, "        <VersionMinor>%d</VersionMinor>\n", PROG_VER_MINOR);
    fprintf(pArgs->outFile, "      </Version>\n");
    fprintf(pArgs->outFile, "    </Build>\n");
    fprintf(pArgs->outFile, "    <LangID>en</LangID>\n");
    fprintf(pArgs->outFile, "  </Author>\n");
    fprintf(pArgs->outFile, "</TrainingCenterDatabase>\n");
}

void printOutput(GpsTrk *pTrk, CmdArgs *pArgs)
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


