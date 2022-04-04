# gpxFileTool

## Intro
gpxFileTool is a simple tool for manipulating GPX and TCX files. While it is generic enough to work with any type of activity, it was developed mainly to process GPX files from *cycling* activities recorded by devices such as a Garmin Edge or Wahoo Elemnt bike computer, or by mobile apps such as Strava or RideWithGps.
 
The GPS elevation data in these GPX files can be subject to significant errors, which result in incorrect values for the total elevation gain/loss of the ride, and in incorrect values for the grade level (slope) during a climb/descent segment of the ride.  

Having an incorrect value for the total elevation gain/loss simply skews one's own personal statistics.  But the incorrect grade level is a problem when such GPX file is used to control an indoor cycling "smart trainer".  The bogus elevation values can result in spikes in the grade level that make the feeling of the *virtual ride* unrealistic, and in extreme cases it can suddenly **lock up** the smart trainer.

One of the design goals for the gpxFileTool is to allow the user to correct these errors, so that the virtual ride on the smart trainer is more realistic. 

The tool has the following features:

1. Can trim out a range of points.
2. Can smooth out the elevation or grade values.
3. Can limit the min/max grade level.
4. Can filter out optional sensor data.
5. Can generate a new GPX, TCX, or CSV file.

Trimming out a range of points is useful to remove such things as "red light", "photo shot", or "nature break" stops during a ride.

Smoothing out the elevation or grade values is the main task when preparing a GPX file for a virtual route. The tool uses a Simple Moving Average (SMA) algorithm, over a configurable range of points, to do the elevation smoothing.

Limiting the min/max grade levels is useful when the user knows *a priori* what those limits are for the given route.

Filtering out optional metrics is useful to remove unwanted sensor data, such as cadence, heart rate, or power.

Being able to generate a CSV file allows the file to be processed by an app such as Excel or LibreOffice, to do detailed data analysis and visualization.

## Building the tool

To build the gpxFileTool binary all you need to do is run 'make' at the top-level directory.

```
$ make
cc -D_GNU_SOURCE -I. -ggdb -Wall -Werror -O3 -o main.o -c main.c
rm -f build_info.c
/bin/sh -ec 'echo "const char *buildInfo = \"built on `date` by `whoami`@`hostname`\";" >> build_info.c'
cc -D_GNU_SOURCE -I. -ggdb -Wall -Werror -O3 -o ./build_info.o -c build_info.c
rm -f build_info.c
cc -ggdb  -o ./gpxFileTool ./main.o ./build_info.o -lm
```
The tool is known to build warning and error free under Ubuntu, OS/X, and Cygwin.

## About GPX Files

GPX files are plain text files that use XML encoding based on the following [data schema](http://www.topografix.com/GPX/1/1/gpx.xsd). 

In a nutshell, a GPX file contains a "track", which contains one or more "track segments", which contain the actual "track points". Each track point includes the GPS coordinates (latitude, longitude, elevation) plus an optional timing data.  Whether or not the GPX file includes this timing data, is the main difference between a GPX *ride* and a GPX *route*. A GPX route is typically created using a mapping app, such as RideWithGps or Strava, while a GPX ride is typically created by a bike computer or a cycling app during an actual activity.   

Below you can see a clip from a GPX ride showing the general structure of the data:

```xml
  <trk>
    <trkseg>
      <trkpt lat="43.7689000000" lon="-114.2755600000">
        <ele>1960.0000000000</ele>
        <time>2021-03-28T14:17:42.010Z</time>
      </trkpt>
      <trkpt lat="43.7692400000" lon="-114.2753200000">
        <ele>1960.2000000000</ele>
        <time>2021-03-28T14:17:54.744Z</time>
      </trkpt>
          .
          .
          .
      <trkpt lat="43.7889600000" lon="-114.2603500000">
        <ele>2121.9000000000</ele>
        <time>2021-03-28T14:30:30.336Z</time>
      </trkpt>
    </trkseg>
  </trk>
```
The latitude and longitude values are expressed in decimal degrees, the elevation in meters, and the time in UTC.

## Examples

The following examples show how to use the tool.  Running the tool with the option --help will show a "manual page" describing all the options: 

```
SYNTAX:
    gpxFileTool [OPTIONS] <file> [<file2> ...]

    When multiple input files are specified, the tool will attempt to
    stitch them together into a single output file.

OPTIONS:
    --activity-type {ride|hike|run|walk|vride|other}
        Specifies the type of activity in the output file. By default the
        output file inherits the activity type of the input file.
    --close-gap <point>
        Close the time gap at the specified track point.
    --help
        Show this help and exit.
    --max-grade <value>
        Limit the maximum grade to the specified value. The elevation
        values are adjusted accordingly.
    --min-grade <value>
        Limit the minimum grade to the specified value. The elevation
        values are adjusted accordingly.
    --name <name>
        String to use for the <name> tag of the track in the output
        file.
    --output-file <name>
        Write the output data into the specified file. If not specified
        the output data is written to standard output.
    --output-filter <mask>
        A bit mask that specifies the set of optional metrics to be
        suppressed from the output. By default, all available optional
        metrics are included in the output.
            0x01 - Ambient Temperature
            0x02 - Cadence
            0x04 - Heart Rate
            0x08 - Power
    --output-format {csv|gpx|shiz|tcx}
        Specifies the format of the output data.
    --quiet
        Suppress all warning messages.
    --range <a,b>
        Limit the track points to be processed to the range between point
        'a' and point 'b', inclusive.
    --rel-time {sec|hms}
        Use relative timestamps in the CSV output, using the specified
        format.
    --set-speed <avg-speed>
        Use the specified average speed value (in km/h) to generate missing
        timestamps, or to replace the existing timestamps, in the input file.
    --sma-metric {elevation|grade|power}
        Specifies the metric to be smoothed out by the Simple Moving Average.
    --sma-window <size>
        Size of the window used to compute the Simple Moving Average
        of the selected values, in order to smooth them out. It must be
        an odd value.
    --start-time <time>
        Start time for the activity (in UTC time). The timestamp of each
        point is adjusted accordingly. Format is: 2018-01-22T10:01:10Z.
    --summary
        Print only a summary of the activity metrics in human-readable
        form and exit.
    --trim
        Trim all the points in the specified range. The timestamps of
        the points after point 'b' are adjusted accordingly, to avoid
        a discontinuity in the time sequence.
    --verbatim
        Process the input file(s) verbatim, without making any adjust-
        ments to the data.
    --version
        Show version information and exit.
```

#### Example 1

In this example we process a GPX file created by a Wahoo Elemnt BOLT bike computer, and simply print a summary of its data.

```
$ gpxFileTool --summary SampleGpxFiles/WahooElmntBolt.gpx
INFO: Discarding duplicate TrkPt #9 (SampleGpxFiles/WahooElmntBolt.gpx:82) !
INFO: Discarding duplicate TrkPt #10 (SampleGpxFiles/WahooElmntBolt.gpx:91) !
INFO: Discarding duplicate TrkPt #11 (SampleGpxFiles/WahooElmntBolt.gpx:100) !
INFO: Discarding duplicate TrkPt #12 (SampleGpxFiles/WahooElmntBolt.gpx:109) !
INFO: Discarding duplicate TrkPt #13 (SampleGpxFiles/WahooElmntBolt.gpx:118) !
INFO: Discarding duplicate TrkPt #14 (SampleGpxFiles/WahooElmntBolt.gpx:127) !
INFO: Discarding duplicate TrkPt #15 (SampleGpxFiles/WahooElmntBolt.gpx:136) !
INFO: Discarding duplicate TrkPt #16 (SampleGpxFiles/WahooElmntBolt.gpx:145) !
INFO: Discarding duplicate TrkPt #17 (SampleGpxFiles/WahooElmntBolt.gpx:154) !
INFO: Discarding duplicate TrkPt #18 (SampleGpxFiles/WahooElmntBolt.gpx:163) !
    numTrkPts: 154
 numDupTrkPts: 10
numTrimTrkPts: 0
numDiscTrkPts: 0
   numElevAdj: 0
  dateAndTime: 2022-03-24T02:49:14
  elapsedTime: 00:02:33
    totalTime: 00:02:33
   movingTime: 00:02:33
  stoppedTime: 00:00:00
     distance: 0.7779815602 km
     elevGain: 7.0000000000 m
     elevLoss: 13.6000000000 m
    maxDeltaD: 9.070 m at TrkPt #82 (SampleGpxFiles/WahooElmntBolt.gpx:739) : time = 81 s, distance = 0.376 km
    maxDeltaT: 11.000 sec at TrkPt #19 (SampleGpxFiles/WahooElmntBolt.gpx:172) : time = 18 s, distance = 0.005 km
     maxSpeed: 32.6514947375 km/h at TrkPt #82 (SampleGpxFiles/WahooElmntBolt.gpx:739) : time = 81 s, distance = 0.376 km, deltaD = 9.070 m, deltaT = 1.000 s
     maxGrade: 9.88% at TrkPt #62 (SampleGpxFiles/WahooElmntBolt.gpx:559) : time = 61 s, distance = 0.253 km, run = 2.024 m, rise = 0.200 m
     minGrade: -11.39% at TrkPt #83 (SampleGpxFiles/WahooElmntBolt.gpx:748) : time = 82 s, distance = 0.384 km, run = 8.779 m, rise = -1.000 m
```

The informational messages about the duplicate track points indicate that the bike computer was not moving at the time, hence the identical GPS data for all those points. The tool automatically removes these duplicate track points, as they do not add any useful information to the track.  Informational and warning messages can be disabled by running the tool with the --quiet option:


```
$ gpxFileTool --quiet --summary SampleGpxFiles/WahooElmntBolt.gpx
    numTrkPts: 154
 numDupTrkPts: 10
numTrimTrkPts: 0
numDiscTrkPts: 0
   numElevAdj: 0
  dateAndTime: 2022-03-24T02:49:14
  elapsedTime: 00:02:33
    totalTime: 00:02:33
   movingTime: 00:02:33
  stoppedTime: 00:00:00
     distance: 0.7779815602 km
     elevGain: 7.0000000000 m
     elevLoss: 13.6000000000 m
    maxDeltaD: 9.070 m at TrkPt #82 (SampleGpxFiles/WahooElmntBolt.gpx:739) : time = 81 s, distance = 0.376 km
    maxDeltaT: 11.000 sec at TrkPt #19 (SampleGpxFiles/WahooElmntBolt.gpx:172) : time = 18 s, distance = 0.005 km
     maxSpeed: 32.6514947375 km/h at TrkPt #82 (SampleGpxFiles/WahooElmntBolt.gpx:739) : time = 81 s, distance = 0.376 km, deltaD = 9.070 m, deltaT = 1.000 s
     maxGrade: 9.88% at TrkPt #62 (SampleGpxFiles/WahooElmntBolt.gpx:559) : time = 61 s, distance = 0.253 km, run = 2.024 m, rise = 0.200 m
     minGrade: -11.39% at TrkPt #83 (SampleGpxFiles/WahooElmntBolt.gpx:748) : time = 82 s, distance = 0.384 km, run = 8.779 m, rise = -1.000 m
```

#### Example 2

There are situations in which one wants to turn a GPX *route* into a GPX *ride*.  For example, imagine you rode your bike for a couple of hours and at the end of the ride you realize you forgot to start your bike computer. Doh!  In this case you can use a mapping app (such as RideWithGPS) to draw the route you rode, export it as a GPX route file, and then add timing data to the GPX route to turn it into a ride, so that it can be uploaded to your Strava account to get distance and elevation gain credits for it.  In this example we take a manually created route, and we turn it into a ride using the current date and time as the activity's start time, and an average speed of 12.5 km/h:

```
$ gpxFileTool --start-time now --set-speed 12.5 SampleGpxFiles/TrailCreekEoP_RWGPS_Route.gpx > outFiles/TrailCreekEoP_RWGPS_Ride.gpx
$ gpxFileTool --summary outFiles/TrailCreekEoP_RWGPS_Ride.gpx
    numTrkPts: 93
 numDupTrkPts: 0
numTrimTrkPts: 0
numDiscTrkPts: 0
   numElevAdj: 0
  dateAndTime: 2022-03-29T22:50:02
  elapsedTime: 00:13:05
    totalTime: 00:13:05
   movingTime: 00:13:05
  stoppedTime: 00:00:00
     distance: 2.7288970354 km
     elevGain: 166.9000000000 m
     elevLoss: 2.4000000000 m
    maxDeltaD: 156.725 m at TrkPt #2 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:22) : time = 45 s, distance = 0.157 km
    maxDeltaT: 45.136 sec at TrkPt #2 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:22) : time = 45 s, distance = 0.157 km
     maxSpeed: 12.5126490732 km/h at TrkPt #43 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:268) : time = 421 s, distance = 1.463 km, deltaD = 2.440 m, deltaT = 0.702 s
     maxGrade: 29.50% at TrkPt #61 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:376) : time = 530 s, distance = 1.841 km, run = 23.390 m, rise = 6.900 m
     minGrade: -4.23% at TrkPt #39 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:244) : time = 398 s, distance = 1.383 km, run = 2.365 m, rise = -0.100 m
```

Notice the high (29.50%) maximum grade value. This is often a by product of poor elevation data in the GPX route file.  This problem can be corrected using the Simple Moving Average (SMA) algorithm, to smooth out the grade values.  Below we use an SMA window size of 5 points, which brings the maximum grade value from 29.50% down to 13.05%.  Notice that the elevation values are adjusted accordingly, leading to a smaller total elevation gain:

```
$ gpxFileTool --start-time now --set-speed 12.5 --sma-metric grade --sma-window 5 SampleGpxFiles/TrailCreekEoP_RWGPS_Route.gpx > outFiles/TrailCreekEoP_RWGPS_Ride.gpx
$ gpxFileTool --summary outFiles/TrailCreekEoP_RWGPS_Ride.gpx
    numTrkPts: 93
 numDupTrkPts: 0
numTrimTrkPts: 0
numDiscTrkPts: 0
   numElevAdj: 0
  dateAndTime: 2022-03-30T02:04:29
  elapsedTime: 00:13:05
    totalTime: 00:13:05
   movingTime: 00:13:05
  stoppedTime: 00:00:00
     distance: 2.7230794755 km
     elevGain: 138.4834820467 m
     elevLoss: 5.6212477973 m
    maxDeltaD: 156.709 m at TrkPt #2 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:22) : time = 45 s, distance = 0.157 km
    maxDeltaT: 45.136 sec at TrkPt #2 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:22) : time = 45 s, distance = 0.157 km
     maxSpeed: 12.5491504737 km/h at TrkPt #92 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:562) : time = 782 s, distance = 2.712 km, deltaD = 8.980 m, deltaT = 2.576 s
     maxGrade: 13.05% at TrkPt #75 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:460) : time = 616 s, distance = 2.135 km, run = 14.813 m, rise = 1.934 m
     minGrade: -8.29% at TrkPt #93 (outFiles/TrailCreekEoP_RWGPS_Ride.gpx:568) : time = 785 s, distance = 2.723 km, run = 11.152 m, rise = -0.924 m
```

To illustrate the effect of using SMA to smooth out the grade, this [graph](https://drive.google.com/file/d/1mO2znGdoVMxzFP7lKB38qx0DUh0HjbTz/view?usp=sharing) shows the raw grade vs. distance, while this [graph](https://drive.google.com/file/d/1agbOHJj0NnI7V62d2hJ7eSB7YxlL2vh9/view?usp=sharing) shows the SMA-smoothed grade using a 3-point window size, and this [graph](https://drive.google.com/file/d/1scB6h1AuwlS4s2fgIpchnZBQqWdEoWgS/view?usp=sharing) using a 5-point window size. 
 
#### Example 3

In this example we instruct the tool to generate a Comma-Separated-Value (CSV) output file, so that the file can be loaded into a spreadsheet app (such as Excel or Libre Office Calc) for further analysis and data visualization:

```
$ gpxFileTool --quiet --output-format csv SampleGpxFiles/WahooElmntBolt.gpx > outFiles/WahooElmntBolt.csv
```
The CSV output file looks like this:

```
<inFile>,<line#>,<trkpt>,<time>,<lat>,<lon>,<ele>,<power>,<atemp>,<cadence>,<hr>,<deltaT>,<run>,<rise>,<dist>,<distance>,<speed>,<grade>
SampleGpxFiles/WahooElmntBolt.gpx,19,2,1648090155,43.6259350000,-114.3518540000,1731.4000000000,0,17,0,0,1.0000000000,0.483,0.0000000000,0.4830733702,0.0004830734,1.7390641328,0.00
SampleGpxFiles/WahooElmntBolt.gpx,28,3,1648090156,43.6259320000,-114.3518520000,1731.4000000000,0,17,0,0,1.0000000000,0.371,0.0000000000,0.3705003177,0.0008535737,1.3338011439,0.00
SampleGpxFiles/WahooElmntBolt.gpx,37,4,1648090157,43.6259310000,-114.3518520000,1731.4000000000,0,17,0,0,1.0000000000,0.111,0.0000000000,0.1112262997,0.0009648000,0.4004146790,0.00
SampleGpxFiles/WahooElmntBolt.gpx,46,5,1648090158,43.6259310000,-114.3518470000,1731.4000000000,0,17,0,0,1.0000000000,0.403,0.0000000000,0.4025611677,0.0013673612,1.4492202037,0.00
SampleGpxFiles/WahooElmntBolt.gpx,55,6,1648090159,43.6259280000,-114.3518450000,1731.4000000000,0,17,0,0,1.0000000000,0.371,0.0000000000,0.3705003224,0.0017378615,1.3338011607,0.00
SampleGpxFiles/WahooElmntBolt.gpx,64,7,1648090160,43.6259290000,-114.3518450000,1731.4000000000,0,17,0,0,1.0000000000,0.111,0.0000000000,0.1112262997,0.0018490878,0.4004146790,0.00
SampleGpxFiles/WahooElmntBolt.gpx,73,8,1648090161,43.6259310000,-114.3518450000,1731.4000000000,0,17,0,0,1.0000000000,0.222,0.0000000000,0.2224526002,0.0020715404,0.8008293606,0.00
  .
  .
  .
```

And this [screenshot](https://drive.google.com/file/d/1w4DPMP_rp_gmzHq6_NTPFvKDd1gI3FT1/view?usp=sharing) shows the graph of elevation and speed vs. distance for this ride, created from the CSV file using the LibreOffice Calc app.

#### Example 4

In this example we stitch together two GPX files from the same activity. This is a common situation when, for example, a long out-and-back ride is interrupted at the turn around point (e.g. to stop for lunch) and the GPS device is stopped to save battery.

```
$ gpxFileTool --summary SampleGpxFiles/Afternoon_Hike_1of2.gpx
    numTrkPts: 219
 numDupTrkPts: 0
numTrimTrkPts: 0
numDiscTrkPts: 0
   numElevAdj: 0
  dateAndTime: 2021-03-29T05:56:45
  elapsedTime: 00:03:39
    totalTime: 00:03:39
   movingTime: 00:03:39
  stoppedTime: 00:00:00
     distance: 0.3066607785 km
     elevGain: 1.1000000000 m
     elevLoss: 0.3000000000 m
    maxDeltaD: 3.840 m at TrkPt #67 (SampleGpxFiles/Afternoon_Hike_1of2.gpx:274) : time = 67 s, distance = 0.080 km
    maxDeltaT: 2.000 sec at TrkPt #2 (SampleGpxFiles/Afternoon_Hike_1of2.gpx:14) : time = 2 s, distance = 0.003 km
     maxSpeed: 13.8249573587 km/h at TrkPt #67 (SampleGpxFiles/Afternoon_Hike_1of2.gpx:274) : time = 67 s, distance = 0.080 km, deltaD = 3.840 m, deltaT = 1.000 s
     maxGrade: 14.88% at TrkPt #41 (SampleGpxFiles/Afternoon_Hike_1of2.gpx:170) : time = 41 s, distance = 0.038 km, run = 0.672 m, rise = 0.100 m
     minGrade: -6.96% at TrkPt #121 (SampleGpxFiles/Afternoon_Hike_1of2.gpx:490) : time = 121 s, distance = 0.161 km, run = 1.437 m, rise = -0.100 m

$ gpxFileTool --summary SampleGpxFiles/Afternoon_Hike_2of2.gpx
    numTrkPts: 200
 numDupTrkPts: 0
numTrimTrkPts: 0
numDiscTrkPts: 0
   numElevAdj: 0
  dateAndTime: 2021-03-29T06:02:59
  elapsedTime: 00:03:21
    totalTime: 00:03:21
   movingTime: 00:03:21
  stoppedTime: 00:00:00
     distance: 0.3403461032 km
     elevGain: 0.3000000000 m
     elevLoss: 1.0000000000 m
    maxDeltaD: 8.628 m at TrkPt #4 (SampleGpxFiles/Afternoon_Hike_2of2.gpx:22) : time = 4 s, distance = 0.021 km
    maxDeltaT: 2.000 sec at TrkPt #3 (SampleGpxFiles/Afternoon_Hike_2of2.gpx:18) : time = 3 s, distance = 0.013 km
     maxSpeed: 31.0613213089 km/h at TrkPt #4 (SampleGpxFiles/Afternoon_Hike_2of2.gpx:22) : time = 4 s, distance = 0.021 km, deltaD = 8.628 m, deltaT = 1.000 s
     maxGrade: 9.49% at TrkPt #15 (SampleGpxFiles/Afternoon_Hike_2of2.gpx:66) : time = 16 s, distance = 0.042 km, run = 1.053 m, rise = 0.100 m
     minGrade: -8.40% at TrkPt #163 (SampleGpxFiles/Afternoon_Hike_2of2.gpx:658) : time = 164 s, distance = 0.284 km, run = 1.190 m, rise = -0.100 m

$ gpxFileTool SampleGpxFiles/Afternoon_Hike_1of2.gpx SampleGpxFiles/Afternoon_Hike_2of2.gpx > outFiles/Afternoon_Hike_Combined.gpx
$ gpxFileTool --summary outFiles/Afternoon_Hike_Combined.gpx
    numTrkPts: 419
 numDupTrkPts: 0
numTrimTrkPts: 0
numDiscTrkPts: 0
   numElevAdj: 0
  dateAndTime: 2021-03-29T12:56:45
  elapsedTime: 00:09:35
    totalTime: 00:09:35
   movingTime: 00:09:35
  stoppedTime: 00:00:00
     distance: 0.6912234432 km
     elevGain: 1.4000000000 m
     elevLoss: 1.6000000000 m
    maxDeltaD: 44.217 m at TrkPt #220 (outFiles/Afternoon_Hike_Combined.gpx:1330) : time = 374 s, distance = 0.351 km
    maxDeltaT: 155.000 sec at TrkPt #220 (outFiles/Afternoon_Hike_Combined.gpx:1330) : time = 374 s, distance = 0.351 km
     maxSpeed: 31.0613213089 km/h at TrkPt #223 (outFiles/Afternoon_Hike_Combined.gpx:1348) : time = 378 s, distance = 0.372 km, deltaD = 8.628 m, deltaT = 1.000 s
     maxGrade: 14.88% at TrkPt #41 (outFiles/Afternoon_Hike_Combined.gpx:256) : time = 41 s, distance = 0.038 km, run = 0.672 m, rise = 0.100 m
     minGrade: -8.40% at TrkPt #382 (outFiles/Afternoon_Hike_Combined.gpx:2302) : time = 538 s, distance = 0.635 km, run = 1.190 m, rise = -0.100 m
```

#### Example 5

In this example we process a GPX file, removing the cadence, heart rate, and power metrics, and create a new TCX file:

```
$ gpxFileTool --quiet --output-filter 0x0f --output-format tcx SampleGpxFiles/FulGaz_Col_de_la_Madone.gpx > outFiles/FulGaz_Col_de_la_Madone.tcx

```     















  





