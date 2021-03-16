# gpxFileTool

## Intro
gpxFileTool is a simple tool for manipulating GPX files. While it is generic enough to work with any type of activity, it was developed mainly to process GPX files from *cycling* activities recorded by such devices as a Garmin Edge or Wahoo Elemnt bike computer, or by mobile apps such as Strava or RideWithGps.
 
The GPS elevation data in these GPX files can be subject to significant errors, which result in incorrect values for the total elevation gain/loss of the ride, and in incorrect values for the grade level during a climb/descent segment of the ride.  

Having an incorrect value for the total elevation gain/loss simply skews one's own personal statistics.  But the incorrect grade level is a problem when such GPX file is used to control a cycling "smart trainer".  The bogus elevation values can result in spikes in the grade level that make the feeling of the *virtual ride* unrealistic, and in extreme cases it can suddenly **lock up** the smart trainer. Imagine you are pedaling your bike on the trainer at a steady pace while climbing a segment with a moderate 4% grade, when all of a sudden you get a spike that sends the grade level to 14% ... Yikes!

One of the design goals for the gpxFileTool is to allow the user to correct these errors, so that the virtual ride on the smart trainer is more realistic. The tool has the following features:

1. Can trim out a range of points.
2. Can smooth out the elevation values.
3. Can limit the min/max grade level.
4. Can filter out optional metrics.
5. Can generate a new GPX file or a CSV file.

Trimming out a range of points is useful to remove such things as "red light" or "nature break" stops during a ride.

Smoothing out the elevation values is the main task when preparing a GPX file for a virtual route. The tool uses a Simple Moving Average (SMA) algorithm, over a configurable range of points, to do the elevation smoothing.

Limiting the min/max grade levels is useful when the user knows *a priori* what those limits are for the given route.

Filtering out optional metrics is useful to remove unwanted sensor data such as heart rate or cadence.

Being able to generate a CSV file allows the file to be processed by an app such as Excel or LibreOffice, to do data analysis, graphing, etc.

In addition, the tool can read the GPX input file from standard input, and it can write the GPX output file to standard output, so that it can be used in a *pipe* to do multiple operations in one shot.

## About GPX Files

GPX files are plain text files that use XML encoding based on the following [data schema](http://www.topografix.com/GPX/1/1/gpx.xsd).


