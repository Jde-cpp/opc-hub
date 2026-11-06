import type Highcharts from 'highcharts/esm/highstock';
import Long from 'long';
import { HistValue, isNoData } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeKey } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { ESeverity, isBad, severity } from '../../../model/status-code';
import { Value } from '../../../model/value';

//The categorical palette of the dataviz reference, light surface:  eight slots, assigned in series order and never cycled, which
//is why NodeHistory caps the selection at eight.
export const trendPalette = ['#2a78d6', '#eb6834', '#1baf7a', '#eda100', '#e87ba4', '#008300', '#4a3aa7', '#e34948'];
//the status colours, reserved - never a series colour:  Uncertain wears warning, Bad critical.  Neither carries the meaning alone:
//the triangle and the flag's text do.
export const uncertainColor = '#fab219', badColor = '#d03b3b';

export type TrendPoint = Highcharts.PointOptionsObject & { custom:{status:number; hold?:boolean; heartbeat?:boolean} };
export type TrendFlag = { x:number; title:string; text:string; custom:{status:number} };

//A line of the trend:  the key its values carry (trendKey), its Highcharts id - a string, as NodeKey is a Symbol - and the legend's
//name.  The History tab's are its nodes (nodeSeries);  the watch list's are its rows, which carry their own key in `series`,
//since two of its connections may hold one node id (#420).
export type TrendSeries = { key:NodeKey; id:string; name:string };
export function nodeSeries( v:Variable ):TrendSeries{ return { key: v.key, id: v.nodeId.uaString(), name: v.name }; }
export function trendKey( v:HistValue ):NodeKey{ return v.series ?? v.node.key; }

//what the trend draws of a value:  a number, a boolean as 1/0, a Long as its double;  anything else is for the table alone.
export function trendY( value:Value|undefined ):number|undefined{
	if( typeof value=="number" )
		return value;
	if( typeof value=="boolean" )
		return value ? 1 : 0;
	if( value instanceof Long )
		return value.toNumber();
	return undefined;
}
//A series' values as Highcharts points - a node's, or a watch row's.  Good is a plain point;  Uncertain keeps its value under a triangle marker;  Bad is a null,
//which breaks the line into the gap the plan asks for, and a flag on the series where the gap opens names the status:  one a run
//of Bad values with that status, since an aggregate read answers every interval of a failure, and none for Bad_NoData, an
//interval with no record, which the gap says alone.  Stepped, a value holds until the next, so a Bad is preceded by a point
//holding the last value at the Bad's time - without it the step would end at the last good reading and the gap would start
//early.  A value with no source time or no numeric value draws nothing.  A heartbeat, a group's stored repeat of its last value
//(spec *Compression*), is a ring in the line's colour, `color`, so a flat line of them reads as a feed that is up, not a dead one,
//and the tooltip says so;  an Uncertain one keeps the triangle.  A point alone between gaps is a marker.
export function toTrendPoints( values:HistValue[], series:{key:NodeKey}, stepped:boolean, color?:string ):{points:TrendPoint[]; flags:TrendFlag[]}{
	const points:TrendPoint[] = [], flags:TrendFlag[] = [];
	let lastY:number|undefined, lastBad:number|undefined;
	for( const v of values ){
		if( !v.source || trendKey( v )!=series.key )
			continue;
		const x = v.source.getTime();
		if( isBad(v.status) ){
			if( stepped && lastY!==undefined )
				points.push( {x, y: lastY, marker: {enabled: false}, custom: {status: v.status, hold: true}} );
			points.push( {x, y: null, custom: {status: v.status}} );
			if( v.status!==lastBad && !isNoData(v.status) )
				flags.push( {x, title: '!', text: OpcError.text(v.status), custom: {status: v.status}} );
			lastBad = v.status;
			lastY = undefined;
			continue;
		}
		lastBad = undefined;
		const y = trendY( v.value );
		if( y===undefined )
			continue;
		const point:TrendPoint = {x, y, custom: {status: v.status}};
		if( v.heartbeat )
			point.custom.heartbeat = true;
		if( severity(v.status)==ESeverity.Uncertain )
			point.marker = { enabled: true, symbol: 'triangle', radius: 5, fillColor: uncertainColor, lineColor: uncertainColor };
		else if( v.heartbeat )
			point.marker = { enabled: true, symbol: 'circle', radius: 3, fillColor: 'transparent', lineColor: color, lineWidth: 1.5 };
		points.push( point );
		lastY = y;
	}
	//a point with no drawn neighbour has no segment to show it:  it wears the line's marker, or a lone push - a setpoint's - drew
	//nothing at all (420 #4)
	points.forEach( (p, i)=>{
		if( p.y!=null && !p.marker && points[i-1]?.y==null && points[i+1]?.y==null )
			p.marker = { enabled: true };
	} );
	return { points, flags };
}