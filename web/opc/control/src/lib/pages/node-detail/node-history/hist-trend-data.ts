import type Highcharts from 'highcharts/esm/highstock';
import Long from 'long';
import { HistValue, isNoData } from '../../../model/hist';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { ESeverity, isBad, severity } from '../../../model/status-code';
import { Value } from '../../../model/value';

//The categorical palette of the dataviz reference, light surface:  eight slots, assigned in series order and never cycled, which
//is why NodeHistory caps the selection at eight.
export const trendPalette = ['#2a78d6', '#eb6834', '#1baf7a', '#eda100', '#e87ba4', '#008300', '#4a3aa7', '#e34948'];
//the status colours, reserved - never a series colour:  Uncertain wears warning, Bad critical.  Neither carries the meaning alone:
//the triangle and the flag's text do.
export const uncertainColor = '#fab219', badColor = '#d03b3b';

export type TrendPoint = Highcharts.PointOptionsObject & { custom:{status:number; hold?:boolean} };
export type TrendFlag = { x:number; title:string; text:string; custom:{status:number} };

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
//A node's values as Highcharts points.  Good is a plain point;  Uncertain keeps its value under a triangle marker;  Bad is a null,
//which breaks the line into the gap the plan asks for, and a flag on the series where the gap opens names the status:  one a run
//of Bad values with that status, since an aggregate read answers every interval of a failure, and none for Bad_NoData, an
//interval with no record, which the gap says alone.  Stepped, a value holds until the next, so a Bad is preceded by a point
//holding the last value at the Bad's time - without it the step would end at the last good reading and the gap would start
//early.  A value with no source time or no numeric value draws nothing.
export function toTrendPoints( values:HistValue[], node:NodeId, stepped:boolean ):{points:TrendPoint[]; flags:TrendFlag[]}{
	const points:TrendPoint[] = [], flags:TrendFlag[] = [];
	let lastY:number|undefined, lastBad:number|undefined;
	for( const v of values ){
		if( !v.source || v.node.key!=node.key )
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
		if( severity(v.status)==ESeverity.Uncertain )
			point.marker = { enabled: true, symbol: 'triangle', radius: 5, fillColor: uncertainColor, lineColor: uncertainColor };
		points.push( point );
		lastY = y;
	}
	return { points, flags };
}