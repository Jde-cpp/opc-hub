import { OeeBucket, oeeCount, oeeDuration, OeeLoss, OeeOccurrence, oeePercent, OeeSegment, OeeSpan, OeeTimeline } from '../../../model/oee';

//What the machine page's three charts draw (#405, docs/oee/spec.md *The Gantt chart*), as plain data a spec reads:  the Gantt
//chart's bands, the Pareto's columns and the trend's.  The colours are fixed to what a band is, never to its order, checked as
//a set by the dataviz validator (light surface, every pair):  the small stop's pink against running's green is its one pair
//under the colour-blind target, so no band says what it is by colour alone - the legend names each, the hover says it, and the
//stops table has every stop.
export const oeeKinds = ['running', 'Unplanned', 'Planned', 'Excluded', 'uncoded', 'small', 'unknown'] as const;
export type OeeKind = typeof oeeKinds[number];
export const oeeKindColors:Record<OeeKind,string> = { running: '#1baf7a', Unplanned: '#d03b3b', Planned: '#2a78d6', Excluded: '#4a3aa7', uncoded: '#eda100', small: '#e87ba4', unknown: '#9aa0ab' };
export const oeeKindLabels:Record<OeeKind,string> = { running: 'Running', Unplanned: 'Unplanned stop', Planned: 'Planned stop', Excluded: 'Excluded', uncoded: 'Uncoded stop', small: 'Small stop', unknown: 'Unknown' };
export const plannedColor = '#7d8594', trendColor = '#2a78d6';

export type OeeLegendItem = { color:string; label:string };
//a mark's hover, as Highcharts' tooltip takes it, and the same in a line for a screen reader
export type OeeTip = { tip:string; text:string };
function tip( lines:(string|undefined)[] ):OeeTip{
	const said = lines.filter( ( l ):l is string=>!!l );
	const escape = ( s:string )=>s.replace( /&/g, '&amp;' ).replace( /</g, '&lt;' ).replace( />/g, '&gt;' );
	return { tip: said.map( ( l, i )=>i ? escape( l ) : `<b>${escape( l )}</b>` ).join( '<br/>' ), text: said.join( ', ' ) };
}
const clock = ( d:Date )=>d.toLocaleString( undefined, {month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit'} );
const spanText = ( s:OeeSpan )=>`${clock( s.start )} to ${clock( s.end )}, ${oeeDuration( s.end.getTime()-s.start.getTime() )}`;

//What a segment is drawn as:  running, unknown, or a stop in its reason's category, and with no reason a small one and an
//uncoded one each in a colour of its own.  The gateway marks a small stop with no reason uncoded too:  small says more.
//An unknown span carries its row's id, as a stop does, and is no stop:  codable() is what a click opens.
export function segmentKind( s:OeeSegment ):OeeKind{
	if( s.state!='stopped' )
		return s.state;
	const category = s.reason?.category;
	return category=='Unplanned' || category=='Planned' || category=='Excluded' ? category : s.small ? 'small' : 'uncoded';
}
export function codable( s:OeeSegment ):boolean{ return s.state=='stopped' && s.stop!=undefined; }

//A band of the Gantt chart:  `y` 0 on the planned row, 1 on the state row.  A state band carries its segment, which a click
//on a stop's opens the coding dialog with.
export type GanttPoint = { x:number; x2:number; y:number; color:string; name:string; custom:OeeTip & {kind?:OeeKind; segment?:OeeSegment} };
export type GanttData = { planned:GanttPoint[]; state:GanttPoint[]; legend:OeeLegendItem[] };
//an occurrence less its breaks, within the range:  the pieces of planned time
export function plannedPieces( o:OeeOccurrence, range:OeeSpan ):OeeSpan[]{
	const lo = Math.max( o.start.getTime(), range.start.getTime() ), hi = Math.min( o.end.getTime(), range.end.getTime() );
	const pieces:OeeSpan[] = [];
	let from = lo;
	for( const b of [...o.breaks].sort( ( a, b )=>a.start.getTime()-b.start.getTime() ) ){
		const start = Math.max( b.start.getTime(), lo ), end = Math.min( b.end.getTime(), hi );
		if( start>=end )
			continue;
		if( start>from )
			pieces.push( {start: new Date( from ), end: new Date( start )} );
		from = Math.max( from, end );
	}
	if( hi>from )
		pieces.push( {start: new Date( from ), end: new Date( hi )} );
	return pieces;
}
export function toGantt( timeline:OeeTimeline, range:OeeSpan ):GanttData{
	const planned = timeline.planned.flatMap( o=>plannedPieces( o, range ).map( ( p ):GanttPoint=>{
		const name = o.name ?? ( o.shift==undefined ? 'Planned' : `Shift ${o.shift}` );
		return { x: p.start.getTime(), x2: p.end.getTime(), y: 0, color: plannedColor, name, custom: tip( [name, spanText( p )] ) };
	} ) );
	const state = timeline.segments.filter( s=>s.end>s.start ).map( ( s ):GanttPoint=>{
		const kind = segmentKind( s );
		const name = s.state=='stopped' ? s.reason?.name ?? oeeKindLabels[kind] : oeeKindLabels[kind];
		const what = s.state=='stopped' && s.reason ? `${s.reason.category}${s.small ? ', a small stop' : ''}` : s.state=='stopped' && s.small && kind!='small' ? 'A small stop' : undefined;
		const coded = s.coding ? [s.coding.comment, s.coding.userName ? `Coded by ${s.coding.userName}` : undefined] : [];
		return { x: s.start.getTime(), x2: s.end.getTime(), y: 1, color: oeeKindColors[kind], name, custom: { ...tip( [name, what, spanText( s ), ...coded, codable( s ) ? 'Click to code the stop' : undefined] ), kind, segment: s } };
	} );
	const used = new Set( state.map( p=>p.custom.kind ) );
	return { planned, state, legend: [...( planned.length ? [{color: plannedColor, label: 'Planned time'}] : [] ), ...oeeKinds.filter( k=>used.has( k ) ).map( k=>({color: oeeKindColors[k], label: oeeKindLabels[k]}) )] };
}

//A column:  `label` its direct label, where the chart has room for them.
export type ColumnPoint = { y:number|null; color:string; custom:OeeTip & {label:string} };
export type ColumnData = { categories:string[]; points:ColumnPoint[]; legend:OeeLegendItem[] };
//The losses, longest first as the gateway answers them, each a column of minutes in its category's colour.  The hover carries
//what a Pareto's second axis would:  the row's share of the lost time and the share down to it.
export function toPareto( losses:OeeLoss[] ):ColumnData{
	const rows = losses.filter( l=>l.ms>0 ), all = rows.reduce( ( sum, l )=>sum+l.ms, 0 );
	let cumulative = 0;
	const kinds:OeeKind[] = [];
	const points = rows.map( ( l ):ColumnPoint=>{
		const category = l.reason?.category;
		const kind:OeeKind = l.kind=='small' ? 'small' : l.kind=='uncoded' ? 'uncoded' : category=='Planned' || category=='Excluded' ? category : 'Unplanned';
		if( !kinds.includes( kind ) )
			kinds.push( kind );
		cumulative += l.ms;
		const name = lossName( l ), length = oeeDuration( l.ms );
		return { y: l.ms/60000, color: oeeKindColors[kind], custom: { ...tip( [name, `${length} in ${l.stops} ${l.stops==1 ? 'stop' : 'stops'}`, `${oeePercent( l.ms/all )} of the lost time, ${oeePercent( cumulative/all )} down to here`] ), label: length } };
	} );
	return { categories: rows.map( lossName ), points, legend: oeeKinds.filter( k=>kinds.includes( k ) ).map( k=>({color: oeeKindColors[k], label: oeeKindLabels[k]}) ) };
}
export function lossName( l:OeeLoss ):string{ return l.kind=='small' ? 'Small stops' : l.kind=='uncoded' ? 'Uncoded' : l.reason?.name ?? 'Reason'; }

//The trend:  a column of OEE, in percent, per bucket of `oee` by shift or day, none where the bucket has no planned time.
//`day` names a bucket's day on the axis.
export type OeeTrendBy = 'shift'|'day';
export function toOeeTrend( buckets:OeeBucket[], by:OeeTrendBy, day=( d:Date )=>d.toLocaleDateString( undefined, {month: 'short', day: 'numeric'} ) ):ColumnData{
	const categories = buckets.map( b=>by=='shift' && b.shift ? `${b.shift} ${day( b.start )}` : day( b.start ) );
	const points = buckets.map( ( b, i ):ColumnPoint=>({
		y: b.oee==undefined ? null : Number( ( b.oee*100 ).toFixed( 1 ) ), color: trendColor,
		custom: { ...tip( [categories[i], `OEE ${oeePercent( b.oee )}`, `Availability ${oeePercent( b.availability )}, performance ${oeePercent( b.performance )}, quality ${oeePercent( b.quality )}`,
			`${oeeCount( b.good )} good of ${oeeCount( b.total )}`, b.partial ? 'Partial:  unknown time, or rows still to come' : undefined] ), label: oeePercent( b.oee ) }
	}) );
	return { categories, points, legend: [] };
}