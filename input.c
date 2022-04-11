/*=========================================================================
 *
 *   Filename:           input.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "const.h"
#include "defs.h"
#include "trkpt.h"

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
    spongExit("No active TrkPt !!!", inFile, lineNum, lineBuf);
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
int parseGpxFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile)
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

int parseTcxFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile)
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
