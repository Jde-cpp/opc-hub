import { OeeBucket, oeeCount, OeeMachineTimeline, OeeNamed, OeeSpan } from '../../../model/oee';
import { OeeMachine } from '../../../model/oee-admin';
import { OeeLine, oeeLineTitle } from '../../../model/oee-lines';
import { Gateway } from '../../../services/gateway-service';
import { OeeAdminService } from '../../../services/oee-admin-service';
import { OeeOf, OeeService } from '../../../services/oee-service';
import { GanttData, toGantt, toGanttPairs } from './oee-chart-data';

//What the OEE page is of (#405, #409):  a machine, or a line in its place, with all that differs between the two - what its
//reads take, what it is called, its Gantt chart's read and rows, what stands in the counts' place and the words of its hints.
//The page asks it, so a part of the page has no branch of its own.
export type OeeSubject = {
	row:OeeMachine|OeeLine;
	line?:OeeLine;
	of:OeeOf;//what its reads take
	noun:'machine'|'line';
	title:string;//its heading's, a line's level and name
	own?:OeeNamed;//the machine a stop or a band that names none is of:  a machine's page's own, a line's page none
	status:( gateway:Gateway )=>Promise<{calendar:string}|undefined>;
	figures:( gateway:Gateway, range:OeeSpan )=>Promise<OeeBucket[]>;
	timelines:( gateway:Gateway, range:OeeSpan )=>Promise<OeeMachineTimeline[]>;//one per machine
	gantt:( timelines:OeeMachineTimeline[], range:OeeSpan )=>GanttData & {rows?:string[]};
	ganttText:string;
	counts:{ title:string; column:string; rows:( b:OeeBucket )=>{label:string; text:string}[] };
	countsAdd:boolean;//its counts are in one unit, so the trend's hover says good of total
	hints:{ performance:string; quality:string; over:string };//a factor not measured, and a performance above 100%
	stopColumns:string[];
	settings:{ label:string; tip:string };//the way to its own page
};
const stopColumns = ['start', 'end', 'length', 'reason', 'code', 'comment', 'actions'];

export function oeeMachineSubject( row:OeeMachine, service:OeeService, admin:OeeAdminService ):OeeSubject{
	const own = { id: row.id, name: row.name }, unit = row.countUnit;
	return {
		row, of: row.id, noun: 'machine', title: row.name, own,
		status: gateway=>admin.status( gateway, row.id ),
		figures: ( gateway, range )=>service.figures( gateway, row.id, range ),
		timelines: async ( gateway, range )=>[{ ...await service.timeline( gateway, row.id, range ), machine: own }],
		gantt: ( timelines, range )=>toGantt( timelines[0], range ),
		ganttText: "The machine's planned time on the first row and its state on the second:  running, each stop in its reason's category, uncoded and small stops, and unknown time.  A stop's band opens its coding.",
		counts: { title: 'Counts', column: 'In planned time', rows: b=>[ {label: 'Total', value: b.total}, {label: 'Good', value: b.good}, {label: 'Reject', value: b.reject}, {label: 'Outside planned time', value: b.outside} ]
			.map( c=>({ label: c.label, text: `${oeeCount( c.value )}${unit ? ` ${unit}` : ''}` }) ) },
		countsAdd: true,
		hints: { performance: 'Not measured:  the machine has no count, or no ideal.', quality: 'Not measured:  the machine has no Good or Reject signal.', over: 'Above 100%:  the machine beats its ideal cycle, so the ideal is too slow.' },
		stopColumns,
		settings: { label: 'Machine', tip: "The machine's properties and signals" }
	};
}
//A line's:  each read its machines' combined, its counts in units of their own, so in their place how many machines each factor
//is over, and its stops each naming its machine, coded on it.
export function oeeLineSubject( row:OeeLine, service:OeeService, admin:OeeAdminService ):OeeSubject{
	const of = { line: row.id };
	return {
		row, line: row, of, noun: 'line', title: oeeLineTitle( row ),
		status: gateway=>admin.lineStatus( gateway, row.id ),
		figures: ( gateway, range )=>service.figures( gateway, of, range, undefined, true ),
		timelines: ( gateway, range )=>service.timelines( gateway, row.id, range ),
		gantt: ( timelines, range )=>toGanttPairs( timelines, range ),
		ganttText: "A pair of rows for each machine of the line, its planned time and under it its state:  running, each stop in its reason's category, uncoded and small stops, and unknown time.  A stop's band opens its coding.",
		counts: { title: 'Coverage', column: 'Machines', rows: b=>{
			const over = ( n:number|undefined )=>n==undefined ? '' : `${n} of ${b.machines ?? 0}`;
			return [ {label: 'Machines', text: String( b.machines ?? '' )}, {label: 'Performance over', text: over( b.coverage?.performance )}, {label: 'Quality over', text: over( b.coverage?.quality )} ];
		} },
		countsAdd: false,
		hints: { performance: 'Not measured on every machine:  a machine needs a count and an ideal.  Coverage says how many it is over.', quality: 'Not measured on every machine:  a machine needs a Good or Reject signal.  Coverage says how many it is over.', over: 'Above 100%:  a machine of the line beats its ideal cycle, so its ideal is too slow.' },
		stopColumns: ['machine', ...stopColumns],
		settings: { label: 'Line', tip: "The line's properties and members" }
	};
}