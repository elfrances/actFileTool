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

// FIT SDK files
//#include "fit/decode.c"
#include "fit/fit.c"
#include "fit/fit_example.c"
#include "fit/fit_crc.c"
#include "fit/fit_convert.c"

static int getLine(FILE *fp, char *lineBuf, size_t bufLen, int lineNum)
{
    while (true) {
        if (fgets(lineBuf, bufLen, fp) == NULL)
            return -1;

        lineNum++;

        //fprintf(stdout, "%u: %s", lineNum, lineBuf);

        // Skip blank and XML comment lines
        if ((lineBuf[0] != '\0') && (strstr(lineBuf, "<!--") == NULL))
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

static const char *garminEpoch = "1989-12-31T00:00:00Z";

// Parse the CSV file and create a list of Track Points (TrkPt's)
int parseCsvFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile)
{
    FILE *fp;
    int lineNum = 0;
    static char lineBuf[1024];
    size_t bufLen = sizeof (lineBuf);

    // Open the CSV file for reading
    if ((fp = fopen(inFile, "r")) == NULL) {
        fprintf(stderr, "Failed to open input file %s\n", inFile);
        return -1;
    }

    // Validate the input file. Expected format is:
    //
    // <trkpt>,<inFile>,<line#>,<time>,<lat>,<lon>,<ele>,...
    //   .
    //   .
    //   .
    //
    lineNum = getLine(fp, lineBuf, bufLen, lineNum);
    if ((lineNum < 0) ||
        (strncmp(lineBuf, csvBannerLine, strlen(csvBannerLine)) != 0)) {
        fprintf(stderr, "Input file is not a CSV file !!!\n");
        return -1;
    }

    // Process one line at a time...
    while ((lineNum = getLine(fp, lineBuf, bufLen, lineNum)) != -1) {
        TrkPt *pTrkPt = NULL;
        const char *p = lineBuf;
        time_t timestamp;
        double distance, speed, dummy;

        // Alloc and init new TrkPt object
        if ((pTrkPt = newTrkPt(pTrk->numTrkPts++, inFile, lineNum)) == NULL) {
            fprintf(stderr, "Failed to create TrkPt object !!!\n");
            return -1;
        }

        // Skip the first 3 columns: "<trkpt>,<inFile>,<line#>,"
        for (int n = 0; n < 3; n++, p++) {
            if ((p = strchr(p, ',')) == NULL) {
                fprintf(stderr, "Failed to parse line: %s !!!\n", p);
                return -1;
            }
        }

        // Parse the columns: "<time>,<latitude>,<longitude>,<elevation>,<distance>,<speed>,<power>,<ambTemp>,<cadence>,<heartRate>,<run>,<rise>,<dist>,<grade>"
        if (sscanf(p, "%ld,%le,%le,%le,%le,%le,%d,%d,%d,%d,%le,%le,%le,%le",
                   &timestamp, &pTrkPt->latitude, &pTrkPt->longitude, &pTrkPt->elevation,
                   &distance, &speed,
                   &pTrkPt->power, &pTrkPt->ambTemp, &pTrkPt->cadence, &pTrkPt->heartRate,
                   &dummy, &dummy, &dummy,
                   &pTrkPt->grade) != 14) {
            fprintf(stderr, "Failed to parse line: %s !!!\n", p);
            return -1;
        }

        pTrkPt->timestamp = (double) timestamp;
        pTrkPt->distance = kmToM(distance); // convert to meters
        pTrkPt->speed = kphToMps(speed);    // convert to m/s

        //printf("%u: time=%.3lf lat=%.10lf lon=%.10lf ele=%.3lf dst=%.3lf speed=%.3lf\n",
        //       pTrkPt->index, pTrkPt->timestamp, pTrkPt->latitude, pTrkPt->longitude,
        //       pTrkPt->elevation, pTrkPt->distance, pTrkPt->speed);

        // Insert track point at the tail of the queue
        TAILQ_INSERT_TAIL(&pTrk->trkPtList, pTrkPt, tqEntry);

        pTrkPt = NULL;
    }

    // If no explicit output format has been specified,
    // use the same format as the input file.
    if (pArgs->outFmt == nil) {
        pArgs->outFmt = csv;
    }

    fclose(fp);

    return 0;
}

// Parse the FIT file and create a list of Track Points (TrkPt's)
int parseFitFile(CmdArgs *pArgs, GpsTrk *pTrk, const char *inFile)
{
    FILE *fp;
    TrkPt *pTrkPt = NULL;
    FIT_UINT8 inBuf[8];
    FIT_CONVERT_RETURN conRet = FIT_CONVERT_CONTINUE;
    FIT_UINT32 bufSize;
    FIT_UINT32 mesgIndex = 0;
    FIT_MANUFACTURER manufacturer = FIT_MANUFACTURER_INVALID;
    struct tm brkDwnTime = {0};
    time_t timeStampOffset;
    Bool timerRunning = true;

    // Compute the UTC of the Garmin Epoch
    strptime(garminEpoch, "%Y-%m-%dT%H:%M:%S", &brkDwnTime);
    timeStampOffset = mktime(&brkDwnTime);

    // Open the FIT file for reading
    if ((fp = fopen(inFile, "r")) == NULL) {
        fprintf(stderr, "Failed to open input file %s\n", inFile);
        return -1;
    }

    FitConvert_Init(FIT_TRUE);

    while (!feof(fp) && (conRet == FIT_CONVERT_CONTINUE)) {
        for (bufSize = 0; (bufSize < sizeof (inBuf)) && !feof(fp); bufSize++) {
            inBuf[bufSize] = (FIT_UINT8) getc(fp);
        }

        do {
            if ((conRet = FitConvert_Read(inBuf, bufSize)) == FIT_CONVERT_MESSAGE_AVAILABLE) {
                const FIT_UINT8 *mesg = FitConvert_GetMessageData();
                FIT_UINT16 mesgNum = FitConvert_GetMessageNumber();

                //printf("Mesg %d (%d) - ", mesgIndex, mesgNum);

                switch (mesgNum) {
                case FIT_MESG_NUM_FILE_ID: {
                    const FIT_FILE_ID_MESG *id = (FIT_FILE_ID_MESG *) mesg;
                    //printf("File ID: type=%u, number=%u manufacturer=%u\n",
                    //        id->type, id->number, id->manufacturer);
                    manufacturer = id->manufacturer;
                    break;
                }

                case FIT_MESG_NUM_DEVICE_SETTINGS: {
                    //const FIT_DEVICE_SETTINGS_MESG *dev = (FIT_DEVICE_SETTINGS_MESG *) mesg;
                    //printf("Device Settings: utc_offset=%u time_offset=%u:%u clock_time=%u\n",
                    //        dev->utc_offset, dev->time_offset[0], dev->time_offset[1], dev->clock_time);
                    break;
                }

                case FIT_MESG_NUM_USER_PROFILE: {
                    //const FIT_USER_PROFILE_MESG *user_profile = (FIT_USER_PROFILE_MESG *) mesg;
                    //printf("User Profile: weight=%0.1fkg gender=%u age=%u\n",
                    //        user_profile->weight / 10.0f, user_profile->gender, user_profile->age);
                    break;
                }

                case FIT_MESG_NUM_ZONES_TARGET: {
                    //printf("Zones Target: \n");
                    break;
                }

                case FIT_MESG_NUM_HR_ZONE: {
                    //printf("HR Zone: \n");
                    break;
                }

                case FIT_MESG_NUM_POWER_ZONE: {
                    //printf("Power Zone: \n");
                    break;
                }

                case FIT_MESG_NUM_SPORT: {
                    const FIT_SPORT_MESG *sport = (FIT_SPORT_MESG *) mesg;
                    //printf("Sport: sport=%u sub_sport=%u\n", sport->sport, sport->sub_sport);
                    if (sport->sport == FIT_SPORT_RUNNING) {
                        pTrk->actType = run;
                    } else if (sport->sport == FIT_SPORT_CYCLING) {
                        pTrk->actType = ride;
                    } else if (sport->sport == FIT_SPORT_WALKING) {
                        pTrk->actType = walk;
                    } else if (sport->sport == FIT_SPORT_HIKING) {
                        pTrk->actType = hike;
                    } else {
                        pTrk->actType = other;
                    }
                    break;
                }

                case FIT_MESG_NUM_SESSION: {
                    //const FIT_SESSION_MESG *session = (FIT_SESSION_MESG *) mesg;
                    //printf("Session: timestamp=%u start_lat=%d start_long=%d elapsed_time=%d distance=%d num_laps: %d\n",
                    //        session->timestamp, session->start_position_lat, session->start_position_long,
                    //        session->total_elapsed_time, session->total_distance, session->num_laps);
                    break;
                }

                case FIT_MESG_NUM_LAP: {
                    //const FIT_LAP_MESG *lap = (FIT_LAP_MESG *) mesg;
                    //printf("Lap: timestamp=%u start_lat=%d start_long=%d end_lat=%d end_long=%d elapsed_time=%d distance=%d\n",
                    //        lap->timestamp, lap->start_position_lat, lap->end_position_long, lap->end_position_lat, lap->end_position_long,
                    //        lap->total_elapsed_time, lap->total_distance);
                    break;
                }

                case FIT_MESG_NUM_RECORD: {
                    const FIT_RECORD_MESG *record = (FIT_RECORD_MESG *) mesg;
#if 0
                    printf("Record: timestamp=%u latitude=%d longitude=%d distance=%u altitude=%u speed=%u power=%u heart_rate=%u cadence=%u temp=%d",
                            record->timestamp, record->position_lat, record->position_long, record->distance,
                            record->altitude, record->speed, record->power,
                            record->heart_rate, record->cadence,
                            record->temperature);

                    if ((record->compressed_speed_distance[0] != FIT_BYTE_INVALID) ||
                        (record->compressed_speed_distance[1] != FIT_BYTE_INVALID) ||
                        (record->compressed_speed_distance[2] != FIT_BYTE_INVALID)) {
                        static FIT_UINT32 accumulated_distance16 = 0;
                        static FIT_UINT32 last_distance16 = 0;
                        FIT_UINT16 speed100;
                        FIT_UINT32 distance16;

                        speed100 = record->compressed_speed_distance[0] | ((record->compressed_speed_distance[1] & 0x0F) << 8);
                        printf(", speed = %0.2fm/s", speed100 / 100.0f);

                        distance16 = (record->compressed_speed_distance[1] >> 4) | (record->compressed_speed_distance[2] << 4);
                        accumulated_distance16 += (distance16 - last_distance16) & 0x0FFF;
                        last_distance16 = distance16;

                        printf(", distance = %0.3fm", accumulated_distance16 / 16.0f);
                    }
#endif
                    if (timerRunning) {
                        // The Strava app generates a pair of FIT RECORD messages
                        // for each trackpoint (i.e. timestamp). The first one seems
                        // to always have a valid distance value of 0.000, but no
                        // latitude/longitude/altitude values: e.g.
                        //
                        // Mesg 9 (21) - Event: timestamp=1018803532 event=0 event_type=0
                        // Mesg 10 (20) - Record: timestamp=1018803532 distance=0.000
                        // Mesg 11 (20) - Record: timestamp=1018803532 latitude=43.6232699098 longitude=-114.3533090010 enh_altitude=1712.000 speed=0.310
                        // Mesg 12 (20) - Record: timestamp=1018803533 distance=0.000
                        // Mesg 13 (20) - Record: timestamp=1018803533 latitude=43.6232681496 longitude=-114.3533167124 enh_altitude=1712.000 speed=0.112
                        //
                        // So here we detect, and skip, such RECORD messages...
                        if ((manufacturer == FIT_MANUFACTURER_STRAVA) &&
                            ((record->position_lat == FIT_SINT32_INVALID) ||
                             (record->position_long == FIT_SINT32_INVALID) ||
                             (record->enhanced_altitude == FIT_UINT32_INVALID))) {
                            //printf(" *** SKIPPED ***");
                        } else {
                            // Alloc and init new TrkPt object
                            if ((pTrkPt = newTrkPt(pTrk->numTrkPts++, inFile, mesgIndex)) == NULL) {
                                fprintf(stderr, "Failed to create TrkPt object !!!\n");
                                return -1;
                            }

                            pTrkPt->timestamp = (double) ((time_t) record->timestamp + timeStampOffset);

                            if (record->position_lat != FIT_SINT32_INVALID) {
                                pTrkPt->latitude = ((double) record->position_lat / (double) 0x7FFFFFFF) * 180.0;
                            }

                            if (record->position_long != FIT_SINT32_INVALID) {
                                pTrkPt->longitude = ((double) record->position_long / (double) 0x7FFFFFFF) * 180.0;
                            }

                            if (record->distance != FIT_UINT32_INVALID) {
                                pTrkPt->distance = ((double) record->distance / 100.0); // in m
                            }

                            if (record->enhanced_altitude != FIT_UINT32_INVALID) {
                                pTrkPt->elevation = (((double) record->enhanced_altitude / 5.0) - 500.0);    // in m
                            } else if (record->altitude != FIT_UINT16_INVALID) {
                                pTrkPt->elevation = (((double) record->altitude / 5.0) - 500.0);    // in m
                            }

                            if (record->enhanced_speed != FIT_UINT32_INVALID) {
                                pTrkPt->speed = ((double) record->enhanced_speed / 1000.0);  // in m/s
                            } else if (record->speed != FIT_UINT16_INVALID) {
                                pTrkPt->speed = ((double) record->speed / 1000.0);  // in m/s
                            }

                            if (record->grade != FIT_SINT16_INVALID) {
                                pTrkPt->grade = record->grade;
                            }

                            if (record->temperature != FIT_SINT8_INVALID) {
                                pTrkPt->ambTemp = record->temperature;
                                pTrk->inMask |= SD_ATEMP;
                            }

                            if (record->cadence != FIT_UINT8_INVALID) {
                                pTrkPt->cadence = record->cadence;
                                pTrk->inMask |= SD_CADENCE;
                            }

                            if (record->heart_rate != FIT_UINT8_INVALID) {
                                pTrkPt->heartRate = record->heart_rate;
                                pTrk->inMask |= SD_HR;
                            }

                            if (record->power != FIT_UINT16_INVALID) {
                                pTrkPt->power = record->power;
                                pTrk->inMask |= SD_POWER;
                            }

                            // Insert track point at the tail of the queue
                            TAILQ_INSERT_TAIL(&pTrk->trkPtList, pTrkPt, tqEntry);

                            pTrkPt = NULL;
                        }
                    } else {
                        fprintf(stderr, "Hu? Timer not running !!!\n");
                    }
                    //printf("\n");
                    break;
                }

                case FIT_MESG_NUM_EVENT: {
                    const FIT_EVENT_MESG *event = (FIT_EVENT_MESG *) mesg;
                    //printf("Event: timestamp=%u event=%u event_type=%u\n",
                    //        event->timestamp, event->event, event->event_type);
                    if (event->event == FIT_EVENT_TIMER) {
                        if (event->event_type == FIT_EVENT_TYPE_START) {
                            timerRunning = true;
                        } else if (event->event_type == FIT_EVENT_TYPE_STOP) {
                            timerRunning = false;
                        }
                    }
                    break;
                }

                case FIT_MESG_NUM_DEVICE_INFO: {
                    //const FIT_DEVICE_INFO_MESG *device_info = (FIT_DEVICE_INFO_MESG *) mesg;
                    //printf("Device Info: timestamp=%u manufacturer=%u product=%u\n",
                    //        device_info->timestamp, device_info->manufacturer, device_info->product);
                    break;
                }

                case FIT_MESG_NUM_ACTIVITY: {
                    //const FIT_ACTIVITY_MESG *activity = (FIT_ACTIVITY_MESG *) mesg;
                    //printf("Activity: timestamp=%u, type=%u, event=%u, event_type=%u, num_sessions=%u\n",
                    //       activity->timestamp, activity->type,
                    //       activity->event, activity->event_type,
                    //       activity->num_sessions);
                    {
                        FIT_ACTIVITY_MESG old_mesg;
                        old_mesg.num_sessions = 1;
                        FitConvert_RestoreFields(&old_mesg);
                        //printf("Restored num_sessions=1 - Activity: timestamp=%u, actType=%u, event=%u, event_type=%u, num_sessions=%u\n",
                        //       activity->timestamp, activity->type,
                        //       activity->event, activity->event_type,
                        //       activity->num_sessions);
                    }
                    break;
                }

                case FIT_MESG_NUM_FILE_CREATOR: {
                    //const FIT_FILE_CREATOR_MESG *creator = (FIT_FILE_CREATOR_MESG *) mesg;
                    //printf("File Creator: sw_ver=%u hw_ver=%u\n", creator->software_version, creator->hardware_version);
                    break;
                }

                case FIT_MESG_NUM_HRV: {
                    //const FIT_HRV_MESG *hrv = (FIT_HRV_MESG *) mesg;
                    //printf("HRV: time=%.3lf\n", ((double) hrv->time[0] / 1000.0));
                    break;
                }

                case FIT_MESG_NUM_FIELD_DESCRIPTION: {
                    //const FIT_FIELD_DESCRIPTION_MESG *desc = (FIT_FIELD_DESCRIPTION_MESG *) mesg;
                    //printf("Field Description: field_name=%s\n", desc->field_name);
                    break;
                }

                case FIT_MESG_NUM_DEVELOPER_DATA_ID: {
                    //const FIT_DEVELOPER_DATA_ID_MESG *id = (FIT_DEVELOPER_DATA_ID_MESG *) mesg;
                    //printf("Developer Data ID: manufacturer_id=%u\n", id->manufacturer_id);
                    break;
                }

                default:
                    //printf("Unknown\n");
                    break;
                }
            }

            mesgIndex++;
        } while (conRet == FIT_CONVERT_MESSAGE_AVAILABLE);
    }

    fclose(fp);

    if (conRet != FIT_CONVERT_END_OF_FILE) {
        const char *errMsg = NULL;
        if (conRet == FIT_CONVERT_ERROR) {
            errMsg = "Error decoding file";
        } else if (conRet == FIT_CONVERT_CONTINUE) {
            errMsg = "Unexpected end of file";
        } else if (conRet == FIT_CONVERT_DATA_TYPE_NOT_SUPPORTED) {
            errMsg = "File is not FIT";
        } else if (conRet == FIT_CONVERT_PROTOCOL_VERSION_NOT_SUPPORTED) {
            errMsg = "Protocol version not supported";
        }

        fprintf(stderr, "%s !!!\n", errMsg);

        return -1;
    }

    return 0;
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
            // Set the activity actType
            pTrk->actType = type;
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

            // Insert track point at the tail of the queue
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
        if (pTrk->actType == 0) {
            if (strstr(lineBuf, "<Activity Sport=\"Biking\">") != NULL) {
                pTrk->actType = ride;
            } else if (strstr(lineBuf, "<Activity Sport=\"Hiking\">") != NULL) {
                pTrk->actType = hike;
            } else if (strstr(lineBuf, "<Activity Sport=\"Running\">") != NULL) {
                pTrk->actType = run;
            } else if (strstr(lineBuf, "<Activity Sport=\"Walking\">") != NULL) {
                pTrk->actType = walk;
            } else if (strstr(lineBuf, "<Activity Sport=\"Other\">") != NULL) {
                pTrk->actType = other;
            }
            if (pTrk->actType != 0) {
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
            double distance, grade, speed;
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
            } else if (sscanf(lineBuf, " <GradePercent>%le</GradePercent>", &grade) == 1) {
                // Got the grade!
                if (pTrkPt == NULL) {
                    // Hu?
                    noActTrkPt(inFile, lineNum, lineBuf);
                }
                pTrkPt->grade = grade;
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

                // Insert track point at the tail of the queue
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
