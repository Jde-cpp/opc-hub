#pragma once
//Part 13's example historians and the results its Annex A publishes for them, as AggregateExamples.csv (the file
//Part 13 v1.05.07 Annex A points to, http://www.opcfoundation.org/UA/schemas/1.05/AggregateExamples.csv) lists them:
//the raw data of Historians 1-5 with each one's Stepped and AggregateConfiguration, then every expected value of the
//aggregates the first cut supports, with its status and info bits.  Each processed table ran from 12:00:00 to 12:01:40
//at the ProcessingInterval it names (milliseconds).  ProcessedTests parses them.
#include <jde/fwk.h>

namespace Jde::Opc::Hist::Tests::Part13{
	constexpr sv RawData = R"csv(
Generate Test Data for Part 13 Spec
Start of Raw Data Tables

Historian1

Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00,,"Bad_NoData","First archive entry, Point created"
12:00:10,10,"Good",""
12:00:20,20,"Good",""
12:00:30,30,"Good",""
12:00:40,,"Bad","ANNOTATION: Operator 1 Jan-02-2012 8:00:00 Scan failed, Bad data entered ANNOTATION: Jan-04-2012 7:10:00 Value cannot be verified"
12:00:50,50,"Good","ANNOTATION: Engineer1 Jan-04-2012 7:00:00 Scanner fixed"
12:01:00,60,"Good",""
12:01:10,70,"Uncertain","ANNOTATION: Technician_1 Jan-02-2012 8:00:00 Value flagged as questionable"
12:01:20,80,"Good",""
12:01:30,90,"Good",""
,,"No Data","No more entries, awaiting next Value"

Historian2

Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:00,,"Bad_NoData","First archive entry, Point created"
12:00:02,10,"Good",""
12:00:25,20,"Good",""
12:00:28,25,"Good",""
12:00:39,30,"Good",""
12:00:42,undefined,"Bad","Bad quality data received, Bad data entered"
12:00:48,40,"Good","Received Good StatusCode value"
12:00:52,50,"Good",""
12:01:12,60,"Good",""
12:01:17,70,"Uncertain","Value is flagged as questionable"
12:01:23,70,"Good",""
12:01:26,80,"Good",""
12:01:30,90,"Good",""
,,"No Data","No more entries, awaiting next Value"

Historian3

Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00,,"Bad_NoData","First archive entry, Point created"
12:00:02,10,"Good",""
12:00:25,20,"Good",""
12:00:28,25,"Good",""
12:00:39,30,"Good",""
12:00:42,undefined,"Bad","Bad quality data received, Bad data entered"
12:00:48,40,"Good","Received Good StatusCode value"
12:00:52,50,"Good",""
12:01:12,60,"Good",""
12:01:17,70,"Uncertain","Value is flagged as questionable"
12:01:23,70,"Good",""
12:01:26,80,"Good",""
12:01:30,90,"Good",""
,,"No Data","No more entries, awaiting next Value"

Historian4

Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00,,"Bad_NoData","First archive entry, Point created"
12:00:02,true,"Good",""
12:00:25,false,"Good",""
12:00:28,true,"Good",""
12:00:39,true,"Good",""
12:00:42,undefined,"Bad","Bad quality data received, Bad data entered"
12:00:48,true,"Good","Received Good StatusCode value"
12:00:52,false,"Good",""
12:01:12,false,"Good",""
12:01:17,true,"Uncertain","Value is flagged as questionable"
12:01:23,true,"Good",""
12:01:26,false,"Good",""
12:01:30,true,"Good",""
,,"No Data","No more entries, awaiting next Value"

Historian5

Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00,,"Bad_NoData","First archive entry, Point created"
12:00:02,10,"Good",""
12:00:25,20,"Good",""
12:00:28,10,"Good",""
12:00:39,30,"Good",""
12:00:42,undefined,"Bad","Bad quality data received, Bad data entered"
12:00:48,30,"Good","Received Good StatusCode value"
12:00:52,50,"Good",""
12:01:12,30,"Good",""
12:01:17,70,"Uncertain","Value is flagged as questionable"
12:01:23,70,"Good",""
12:01:26,80,"Good",""
12:01:30,70,"Good",""
,,"No Data","No more entries, awaiting next scan"
End of Raw Data Tables
)csv";
	constexpr sv Processed = R"csv(
Aggregate,Interpolative

Historian1

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,,"BadNoData",""
12:00:05.000,,"BadNoData",""
12:00:10.000,10,"Good",""
12:00:15.000,15,"Good, Interpolated",""
12:00:20.000,20,"Good",""
12:00:25.000,25,"Good, Interpolated",""
12:00:30.000,30,"Good",""
12:00:35.000,35,"UncertainDataSubNormal, Interpolated",""
12:00:40.000,40,"UncertainDataSubNormal, Interpolated",""
12:00:45.000,45,"UncertainDataSubNormal, Interpolated",""
12:00:50.000,50,"Good",""
12:00:55.000,55,"Good, Interpolated",""
12:01:00.000,60,"Good",""
12:01:05.000,65,"UncertainDataSubNormal, Interpolated",""
12:01:10.000,70,"Uncertain",""
12:01:15.000,75,"UncertainDataSubNormal, Interpolated",""
12:01:20.000,80,"Good",""
12:01:25.000,85,"Good, Interpolated",""
12:01:30.000,90,"Good",""
12:01:35.000,90,"UncertainDataSubNormal, Interpolated",""

Aggregate,Interpolative

Historian2

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:00.000,,"BadNoData",""
12:00:05.000,11.304,"Good, Interpolated",""
12:00:10.000,13.478,"Good, Interpolated",""
12:00:15.000,15.652,"Good, Interpolated",""
12:00:20.000,17.826,"Good, Interpolated",""
12:00:25.000,20,"Good",""
12:00:30.000,25.909,"Good, Interpolated",""
12:00:35.000,28.182,"Good, Interpolated",""
12:00:40.000,31.111,"UncertainDataSubNormal, Interpolated",""
12:00:45.000,36.667,"UncertainDataSubNormal, Interpolated",""
12:00:50.000,45,"Good, Interpolated",""
12:00:55.000,51.500,"Good, Interpolated",""
12:01:00.000,54,"Good, Interpolated",""
12:01:05.000,56.500,"Good, Interpolated",""
12:01:10.000,59,"Good, Interpolated",""
12:01:15.000,62.727,"UncertainDataSubNormal, Interpolated",""
12:01:20.000,67.273,"UncertainDataSubNormal, Interpolated",""
12:01:25.000,76.667,"Good, Interpolated",""
12:01:30.000,90,"Good",""
12:01:35.000,102.500,"UncertainDataSubNormal, Interpolated",""

Aggregate,Interpolative

Historian3

Processing Interval ,5000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,,"BadNoData",""
12:00:05.000,10,"Good, Interpolated",""
12:00:10.000,10,"Good, Interpolated",""
12:00:15.000,10,"Good, Interpolated",""
12:00:20.000,10,"Good, Interpolated",""
12:00:25.000,20,"Good",""
12:00:30.000,25,"Good, Interpolated",""
12:00:35.000,25,"Good, Interpolated",""
12:00:40.000,30,"Good, Interpolated",""
12:00:45.000,30,"UncertainDataSubNormal, Interpolated",""
12:00:50.000,40,"Good, Interpolated",""
12:00:55.000,50,"Good, Interpolated",""
12:01:00.000,50,"Good, Interpolated",""
12:01:05.000,50,"Good, Interpolated",""
12:01:10.000,50,"Good, Interpolated",""
12:01:15.000,60,"Good, Interpolated",""
12:01:20.000,60,"UncertainDataSubNormal, Interpolated",""
12:01:25.000,70,"Good, Interpolated",""
12:01:30.000,90,"Good",""
12:01:35.000,90,"UncertainDataSubNormal, Interpolated",""

Aggregate,Interpolative

Historian5

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,,"BadNoData",""
12:00:05.000,11.3043,"Good, Interpolated",""
12:00:10.000,13.4783,"Good, Interpolated",""
12:00:15.000,15.6522,"Good, Interpolated",""
12:00:20.000,17.8261,"Good, Interpolated",""
12:00:25.000,20,"Good",""
12:00:30.000,13.6364,"Good, Interpolated",""
12:00:35.000,22.7273,"Good, Interpolated",""
12:00:40.000,30,"UncertainDataSubNormal, Interpolated",""
12:00:45.000,30,"UncertainDataSubNormal, Interpolated",""
12:00:50.000,40,"Good, Interpolated",""
12:00:55.000,47,"Good, Interpolated",""
12:01:00.000,42,"Good, Interpolated",""
12:01:05.000,37,"Good, Interpolated",""
12:01:10.000,32,"Good, Interpolated",""
12:01:15.000,54,"UncertainDataSubNormal, Interpolated",""
12:01:20.000,70,"UncertainDataSubNormal, Interpolated",""
12:01:25.000,76.6667,"Good, Interpolated",""
12:01:30.000,70,"Good",""
12:01:35.000,70,"UncertainDataSubNormal, Interpolated",""

Aggregate,Average

Historian1

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,,"BadNoData",""
12:00:05.000,,"BadNoData",""
12:00:10.000,10,"Good, Calculated",""
12:00:15.000,,"BadNoData",""
12:00:20.000,20,"Good, Calculated",""
12:00:25.000,,"BadNoData",""
12:00:30.000,30,"Good, Calculated",""
12:00:35.000,,"BadNoData",""
12:00:40.000,,"BadNoData",""
12:00:45.000,,"BadNoData",""
12:00:50.000,50,"Good, Calculated",""
12:00:55.000,,"BadNoData",""
12:01:00.000,60,"Good, Calculated",""
12:01:05.000,,"BadNoData",""
12:01:10.000,,"BadNoData",""
12:01:15.000,,"BadNoData",""
12:01:20.000,80,"Good, Calculated",""
12:01:25.000,,"BadNoData",""
12:01:30.000,90,"Good, Calculated",""
12:01:35.000,,"BadNoData",""

Aggregate,Average

Historian2

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated",""
12:00:05.000,,"BadNoData",""
12:00:10.000,,"BadNoData",""
12:00:15.000,,"BadNoData",""
12:00:20.000,,"BadNoData",""
12:00:25.000,22.500,"Good, Calculated",""
12:00:30.000,,"BadNoData",""
12:00:35.000,30,"Good, Calculated",""
12:00:40.000,,"BadNoData",""
12:00:45.000,40,"Good, Calculated",""
12:00:50.000,50,"Good, Calculated",""
12:00:55.000,,"BadNoData",""
12:01:00.000,,"BadNoData",""
12:01:05.000,,"BadNoData",""
12:01:10.000,60,"Good, Calculated",""
12:01:15.000,,"BadNoData",""
12:01:20.000,70,"Good, Calculated",""
12:01:25.000,80,"Good, Calculated",""
12:01:30.000,90,"Good, Calculated",""
12:01:35.000,,"BadNoData",""

Aggregate,Average

Historian3

Processing Interval ,5000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated",""
12:00:05.000,,"BadNoData",""
12:00:10.000,,"BadNoData",""
12:00:15.000,,"BadNoData",""
12:00:20.000,,"BadNoData",""
12:00:25.000,22.500,"Good, Calculated",""
12:00:30.000,,"BadNoData",""
12:00:35.000,30,"Good, Calculated",""
12:00:40.000,,"BadNoData",""
12:00:45.000,40,"Good, Calculated",""
12:00:50.000,50,"Good, Calculated",""
12:00:55.000,,"BadNoData",""
12:01:00.000,,"BadNoData",""
12:01:05.000,,"BadNoData",""
12:01:10.000,60,"Good, Calculated",""
12:01:15.000,,"BadNoData",""
12:01:20.000,70,"Good, Calculated",""
12:01:25.000,80,"Good, Calculated",""
12:01:30.000,90,"Good, Calculated",""
12:01:35.000,,"BadNoData",""

Aggregate,Average

Historian5

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated",""
12:00:05.000,,"BadNoData",""
12:00:10.000,,"BadNoData",""
12:00:15.000,,"BadNoData",""
12:00:20.000,,"BadNoData",""
12:00:25.000,15,"Good, Calculated",""
12:00:30.000,,"BadNoData",""
12:00:35.000,30,"Good, Calculated",""
12:00:40.000,,"BadNoData",""
12:00:45.000,30,"Good, Calculated",""
12:00:50.000,50,"Good, Calculated",""
12:00:55.000,,"BadNoData",""
12:01:00.000,,"BadNoData",""
12:01:05.000,,"BadNoData",""
12:01:10.000,30,"Good, Calculated",""
12:01:15.000,,"BadNoData",""
12:01:20.000,70,"Good, Calculated",""
12:01:25.000,80,"Good, Calculated",""
12:01:30.000,70,"Good, Calculated",""
12:01:35.000,,"BadNoData",""

Aggregate,TimeAverage

Historian1

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,,"BadNoData",""
12:00:05.000,,"BadNoData",""
12:00:10.000,12.500,"Good, Calculated",""
12:00:15.000,17.500,"Good, Calculated",""
12:00:20.000,22.500,"Good, Calculated",""
12:00:25.000,27.500,"Good, Calculated",""
12:00:30.000,32.500,"UncertainDataSubNormal, Calculated",""
12:00:35.000,37.500,"UncertainDataSubNormal, Calculated",""
12:00:40.000,42.500,"UncertainDataSubNormal, Calculated",""
12:00:45.000,47.500,"UncertainDataSubNormal, Calculated",""
12:00:50.000,52.500,"Good, Calculated",""
12:00:55.000,57.500,"Good, Calculated",""
12:01:00.000,62.500,"UncertainDataSubNormal, Calculated",""
12:01:05.000,67.500,"UncertainDataSubNormal, Calculated",""
12:01:10.000,72.500,"UncertainDataSubNormal, Calculated",""
12:01:15.000,77.500,"UncertainDataSubNormal, Calculated",""
12:01:20.000,82.500,"Good, Calculated",""
12:01:25.000,87.500,"Good, Calculated",""
12:01:30.000,90,"UncertainDataSubNormal, Calculated, Partial",""
12:01:35.000,,"BadNoData",""

Aggregate,TimeAverage

Historian2

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:00.000,10.652,"UncertainDataSubNormal, Calculated, Partial",""
12:00:05.000,12.391,"Good, Calculated",""
12:00:10.000,14.565,"Good, Calculated",""
12:00:15.000,16.739,"Good, Calculated",""
12:00:20.000,18.913,"Good, Calculated",""
12:00:25.000,23.682,"Good, Calculated",""
12:00:30.000,27.045,"Good, Calculated",""
12:00:35.000,29.384,"UncertainDataSubNormal, Calculated",""
12:00:40.000,33.889,"UncertainDataSubNormal, Calculated",""
12:00:45.000,40,"UncertainDataSubNormal, Calculated",""
12:00:50.000,49.450,"Good, Calculated",""
12:00:55.000,52.750,"Good, Calculated",""
12:01:00.000,55.250,"Good, Calculated",""
12:01:05.000,57.750,"Good, Calculated",""
12:01:10.000,60.618,"UncertainDataSubNormal, Calculated",""
12:01:15.000,65,"UncertainDataSubNormal, Calculated",""
12:01:20.000,70.515,"UncertainDataSubNormal, Calculated",""
12:01:25.000,83.667,"Good, Calculated",""
12:01:30.000,96.250,"UncertainDataSubNormal, Calculated, Partial",""
12:01:35.000,,"BadNoData",""

Aggregate,TimeAverage

Historian3

Processing Interval ,5000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10.652,"UncertainDataSubNormal, Calculated, Partial",""
12:00:05.000,12.391,"Good, Calculated",""
12:00:10.000,14.565,"Good, Calculated",""
12:00:15.000,16.739,"Good, Calculated",""
12:00:20.000,18.913,"Good, Calculated",""
12:00:25.000,23.682,"Good, Calculated",""
12:00:30.000,27.045,"Good, Calculated",""
12:00:35.000,29.384,"UncertainDataSubNormal, Calculated",""
12:00:40.000,33.889,"UncertainDataSubNormal, Calculated",""
12:00:45.000,40,"UncertainDataSubNormal, Calculated",""
12:00:50.000,49.450,"Good, Calculated",""
12:00:55.000,52.750,"Good, Calculated",""
12:01:00.000,55.250,"Good, Calculated",""
12:01:05.000,57.750,"Good, Calculated",""
12:01:10.000,60.618,"UncertainDataSubNormal, Calculated",""
12:01:15.000,65,"UncertainDataSubNormal, Calculated",""
12:01:20.000,70.515,"UncertainDataSubNormal, Calculated",""
12:01:25.000,83.667,"Good, Calculated",""
12:01:30.000,90,"UncertainDataSubNormal, Calculated, Partial",""
12:01:35.000,,"BadNoData",""

Aggregate,TimeAverage

Historian5

Processing Interval ,5000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10.6522,"UncertainDataSubNormal, Calculated, Partial",""
12:00:05.000,12.3913,"Good, Calculated",""
12:00:10.000,14.5652,"Good, Calculated",""
12:00:15.000,16.7391,"Good, Calculated",""
12:00:20.000,18.9130,"Good, Calculated",""
12:00:25.000,13.7273,"Good, Calculated",""
12:00:30.000,18.1818,"Good, Calculated",""
12:00:35.000,27.0909,"UncertainDataSubNormal, Calculated",""
12:00:40.000,30,"UncertainDataSubNormal, Calculated",""
12:00:45.000,32,"UncertainDataSubNormal, Calculated",""
12:00:50.000,47.1000,"Good, Calculated",""
12:00:55.000,44.500,"Good, Calculated",""
12:01:00.000,39.500,"Good, Calculated",""
12:01:05.000,34.500,"Good, Calculated",""
12:01:10.000,37.6000,"UncertainDataSubNormal, Calculated",""
12:01:15.000,66.8000,"UncertainDataSubNormal, Calculated",""
12:01:20.000,71.3333,"UncertainDataSubNormal, Calculated",""
12:01:25.000,75.6667,"Good, Calculated",""
12:01:30.000,70,"UncertainDataSubNormal, Calculated, Partial",""
12:01:35.000,,"BadNoData",""

Aggregate,Minimum

Historian1

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated, Partial",""
12:00:16.000,20,"Good, Calculated",""
12:00:32.000,,"BadNoData",""
12:00:48.000,50,"Good, Calculated",""
12:01:04.000,,"BadNoData",""
12:01:20.000,80,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Minimum

Historian2

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated, Partial",""
12:00:16.000,20,"Good, Calculated",""
12:00:32.000,30,"UncertainDataSubNormal, Calculated",""
12:00:48.000,40,"Good",""
12:01:04.000,60,"UncertainDataSubNormal, Calculated",""
12:01:20.000,70,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Minimum

Historian3

Processing Interval ,16000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated, Partial",""
12:00:16.000,20,"Good, Calculated",""
12:00:32.000,30,"UncertainDataSubNormal, Calculated",""
12:00:48.000,40,"Good",""
12:01:04.000,60,"UncertainDataSubNormal, Calculated",""
12:01:20.000,70,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Minimum

Historian5

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated, Partial",""
12:00:16.000,10,"Good, Calculated",""
12:00:32.000,30,"UncertainDataSubNormal, Calculated",""
12:00:48.000,30,"Good",""
12:01:04.000,30,"Good, Calculated",""
12:01:20.000,70,"Good, Calculated, MultipleValues, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Maximum

Historian1

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated, Partial",""
12:00:16.000,30,"Good, Calculated",""
12:00:32.000,,"BadNoData",""
12:00:48.000,60,"Good, Calculated",""
12:01:04.000,,"BadNoData",""
12:01:20.000,90,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Maximum

Historian2

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated, Partial",""
12:00:16.000,25,"Good, Calculated",""
12:00:32.000,30,"UncertainDataSubNormal, Calculated",""
12:00:48.000,50,"Good, Calculated",""
12:01:04.000,60,"UncertainDataSubNormal, Calculated",""
12:01:20.000,90,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Maximum

Historian3

Processing Interval ,16000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated, Partial",""
12:00:16.000,25,"Good, Calculated",""
12:00:32.000,30,"UncertainDataSubNormal, Calculated",""
12:00:48.000,50,"Good, Calculated",""
12:01:04.000,60,"UncertainDataSubNormal, Calculated",""
12:01:20.000,90,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Maximum

Historian5

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,10,"Good, Calculated, Partial",""
12:00:16.000,20,"Good, Calculated",""
12:00:32.000,30,"UncertainDataSubNormal, Calculated",""
12:00:48.000,50,"Good, Calculated",""
12:01:04.000,30,"Good, Calculated",""
12:01:20.000,80,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Count

Historian1

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,1,"Good, Calculated, Partial",""
12:00:16.000,2,"Good, Calculated",""
12:00:32.000,,"Bad",""
12:00:48.000,2,"Good, Calculated",""
12:01:04.000,0,"UncertainDataSubNormal, Calculated",""
12:01:20.000,2,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Count

Historian2

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:00.000,1,"Good, Calculated, Partial",""
12:00:16.000,2,"Good, Calculated",""
12:00:32.000,1,"UncertainDataSubNormal, Calculated",""
12:00:48.000,2,"Good, Calculated",""
12:01:04.000,1,"UncertainDataSubNormal, Calculated",""
12:01:20.000,3,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Count

Historian3

Processing Interval ,16000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,1,"Good, Calculated, Partial",""
12:00:16.000,2,"Good, Calculated",""
12:00:32.000,1,"Good, Calculated",""
12:00:48.000,2,"Good, Calculated",""
12:01:04.000,1,"Good, Calculated",""
12:01:20.000,3,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Count

Historian4

Processing Interval ,16000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,1,"Good, Calculated, Partial",""
12:00:16.000,2,"Good, Calculated",""
12:00:32.000,1,"UncertainDataSubNormal, Calculated",""
12:00:48.000,2,"Good, Calculated",""
12:01:04.000,1,"UncertainDataSubNormal, Calculated",""
12:01:20.000,3,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Count

Historian5

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,1,"Good, Calculated, Partial",""
12:00:16.000,2,"Good, Calculated",""
12:00:32.000,1,"UncertainDataSubNormal, Calculated",""
12:00:48.000,2,"Good, Calculated",""
12:01:04.000,1,"UncertainDataSubNormal, Calculated",""
12:01:20.000,3,"Good, Calculated, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Start

Historian1

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:10.000,10,"Good, Partial",""
12:00:20.000,20,"Good",""
12:00:40.000,,"Bad",""
12:00:50.000,50,"Good",""
12:01:10.000,70,"Uncertain",""
12:01:20.000,80,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Start

Historian2

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:02.000,10,"Good, Partial",""
12:00:25.000,20,"Good",""
12:00:39.000,30,"Good",""
12:00:48.000,40,"Good",""
12:01:12.000,60,"Good",""
12:01:23.000,70,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Start

Historian3

Processing Interval ,16000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:02.000,10,"Good, Partial",""
12:00:25.000,20,"Good",""
12:00:39.000,30,"Good",""
12:00:48.000,40,"Good",""
12:01:12.000,60,"Good",""
12:01:23.000,70,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,Start

Historian5

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:02.000,10,"Good, Partial",""
12:00:25.000,20,"Good",""
12:00:39.000,30,"Good",""
12:00:48.000,30,"Good",""
12:01:12.000,30,"Good",""
12:01:23.000,70,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,End

Historian1

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:10.000,10,"Good, Partial",""
12:00:30.000,30,"Good",""
12:00:40.000,,"Bad",""
12:01:00.000,60,"Good",""
12:01:10.000,70,"Uncertain",""
12:01:30.000,90,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,End

Historian2

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:02.000,10,"Good, Partial",""
12:00:28.000,25,"Good",""
12:00:42.000,,"Bad",""
12:00:52.000,50,"Good",""
12:01:17.000,70,"Uncertain",""
12:01:30.000,90,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,End

Historian3

Processing Interval ,16000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:02.000,10,"Good, Partial",""
12:00:28.000,25,"Good",""
12:00:42.000,,"Bad",""
12:00:52.000,50,"Good",""
12:01:17.000,70,"Uncertain",""
12:01:30.000,90,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,End

Historian5

Processing Interval ,16000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:02.000,10,"Good, Partial",""
12:00:28.000,10,"Good",""
12:00:42.000,,"Bad",""
12:00:52.000,50,"Good",""
12:01:17.000,70,"Uncertain",""
12:01:30.000,70,"Good, Partial",""
12:01:36.000,,"BadNoData",""

Aggregate,StandardDeviationSample

Historian1

Processing Interval ,20000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,0,"Good, Calculated, Partial",""
12:00:20.000,7.071,"Good, Calculated",""
12:00:40.000,0,"UncertainDataSubNormal, Calculated",""
12:01:00.000,0,"UncertainDataSubNormal, Calculated",""
12:01:20.000,7.071,"Good, Calculated, Partial",""

Aggregate,StandardDeviationSample

Historian2

Processing Interval ,20000
Stepped ,false
Treat Uncertain as Bad ,true
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,true

Timestamp,Value,StatusCode,Notes
12:00:00.000,0,"Good, Calculated, Partial",""
12:00:20.000,5,"Good, Calculated",""
12:00:40.000,7.071,"UncertainDataSubNormal, Calculated",""
12:01:00.000,0,"UncertainDataSubNormal, Calculated",""
12:01:20.000,10,"Good, Calculated, Partial",""

Aggregate,StandardDeviationSample

Historian3

Processing Interval ,20000
Stepped ,true
Treat Uncertain as Bad ,true
Percent Bad ,50
Percent Good ,50
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,0,"Good, Calculated, Partial",""
12:00:20.000,5,"Good, Calculated",""
12:00:40.000,7.071,"UncertainDataSubNormal, Calculated",""
12:01:00.000,0,"UncertainDataSubNormal, Calculated",""
12:01:20.000,10,"Good, Calculated, Partial",""

Aggregate,StandardDeviationSample

Historian5

Processing Interval ,20000
Stepped ,false
Treat Uncertain as Bad ,false
Percent Bad ,100
Percent Good ,100
Use Sloped Extrapolation ,false

Timestamp,Value,StatusCode,Notes
12:00:00.000,0,"Good, Calculated, Partial",""
12:00:20.000,10,"Good, Calculated",""
12:00:40.000,14.142,"UncertainDataSubNormal, Calculated",""
12:01:00.000,0,"UncertainDataSubNormal, Calculated",""
12:01:20.000,5.7735,"Good, Calculated, Partial",""
)csv";
}