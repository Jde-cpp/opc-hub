import { ISlugRow, Mutation, MutationType, SlugRow, SlugRowProps } from 'jde-framework';
import { HistGroup, HistRef, toHistRef } from './hist-admin';

//OEE's admin tables as the gateway's pages hold them (#404, docs/oee/spec.md *Machines*, *Reasons and codings*):  oee_machines and
//oee_reasons as rows a detail page saves whole, and a machine's signals, oee_machine_signals, as rows edited one at a time, since
//each has a live side - the engine starts again `settle` after the last - and answers the warnings the page shows beside it.

export const msPerMinute = 60000;
//The ideal as the form takes it (spec *Machines*):  a cycle time, seconds a unit, or a rate, units a minute.  The column is the
//cycle, milliseconds a unit.
export type OeeIdealMode = 'cycle'|'rate';
//a rate per minute as the cycle it is, and a cycle as its rate:  the one formula, since each is the minute over the other
export function rateToCycle( perMinute:number ):number|undefined{ return Number.isFinite( perMinute ) && perMinute>0 ? msPerMinute/perMinute : undefined; }
export function cycleToRate( cycleMs:number ):number|undefined{ return rateToCycle( cycleMs ); }
//What is typed as the cycle it stores:  none for a blank, NaN for what is no number above zero.
export function parseOeeIdeal( text:string, mode:OeeIdealMode ):number|undefined{
	const t = text.trim();
	if( !t )
		return undefined;
	const n = Number( t );
	return !Number.isFinite( n ) || n<=0 ? NaN : mode=='rate' ? rateToCycle( n ) : n*1000;
}
//a cycle as the form shows it in `mode`, to six significant digits:  7 a minute is a cycle of 8571.428… ms, and reads 7 again
export function oeeIdealText( cycleMs:number|undefined, mode:OeeIdealMode ):string{
	if( cycleMs==undefined || !Number.isFinite( cycleMs ) || cycleMs<=0 )
		return '';
	return String( Number( (mode=='rate' ? msPerMinute/cycleMs : cycleMs/1000).toPrecision( 6 ) ) );
}

//the widths the form offers (spec *Intervals*), in minutes;  a row with another the engine takes keeps it
export const oeeWidthMinutes = [1, 5, 15, 30];
export const oeeDefaultWidth = msPerMinute, oeeDefaultSmallStop = 300000;

//a column of whole milliseconds as the form holds it:  a number, none for a blank, or the text typed where it is neither
type MsField = number|string|null|undefined;
function ms( v:MsField ):number|string|undefined{
	if( v==null || v==='' )
		return undefined;
	const n = Number( v );
	return Number.isInteger( n ) && n>=0 ? n : String( v );
}
const msError = 'Milliseconds:  a whole number from 0.';

//A machine:  one historian group whose members are given roles, with its thresholds, its rows' width and its shift calendar.
//The group never changes once the row is created - the gateway refuses the update - so the form fixes it on an existing row.
export class OeeMachine extends SlugRow<OeeMachine>{
	constructor( obj:OeeMachineProps ){
		super( OeeMachine.typeName, <SlugRowProps>obj );
		this.group = obj.group ?? toHistRef( obj.histGroup );
		this.histGroupId = HistGroup.toId( obj.histGroupId ) ?? this.group?.id;
		this.calendarId = HistGroup.toId( obj.calendarId );
		this.idealCycle = obj.idealCycle==null ? undefined : Number( obj.idealCycle );
		this.idealInvalid = obj.idealInvalid ?? false;
		this.countUnit = obj.countUnit || undefined;
		this.smallStop = obj.smallStop===undefined && !obj.id ? oeeDefaultSmallStop : obj.smallStop;
		this.idleTimeout = obj.idleTimeout;
		this.width = obj.width==null ? oeeDefaultWidth : Number( obj.width );
		this.processed = obj.processed ? new Date( obj.processed ) : undefined;
	}
	override get canSave():boolean{ return super.canSave && !!this.histGroupId && !this.idealInvalid && !this.fieldError( 'smallStop' ) && !this.fieldError( 'idleTimeout' ); }
	override fieldError( field:string ):string|undefined{
		if( field=='smallStop' ){
			const v = ms( this.smallStop );
			return v===undefined ? 'Required:  0 makes no stop a small one.' : typeof v=='string' ? msError : undefined;
		}
		return field=='idleTimeout' && typeof ms( this.idleTimeout )=='string' ? msError : undefined;
	}
	override equals( row:ISlugRow ):boolean{
		const o = <OeeMachine>row;
		return super.equals( row ) && this.histGroupId==o.histGroupId && this.calendarId==o.calendarId && this.idealCycle==o.idealCycle && this.idealInvalid==o.idealInvalid
			&& (this.countUnit || undefined)==(o.countUnit || undefined) && ms( this.smallStop )==ms( o.smallStop ) && ms( this.idleTimeout )==ms( o.idleTimeout ) && this.width==o.width;
	}
	//A create states the group and each column set;  an update, what changed, null for one cleared.  The group is never sent on an update.
	override mutation( original:OeeMachine ):Mutation[]{
		console.assert( this.canSave );
		const args:Record<string,unknown> = super.mutationArgs( original );
		if( args['slug']!==undefined )
			args['slug'] = String( args['slug'] ).trim();
		const create = !original?.id;
		if( create )
			args['histGroupId'] = this.histGroupId;
		const set = ( name:string, now:unknown, was:unknown )=>{
			if( create ? now!=undefined : now!=was )
				args[name] = now ?? null;
		};
		set( 'calendarId', this.calendarId, original?.calendarId );
		set( 'idealCycle', this.idealCycle, original?.idealCycle );
		set( 'countUnit', this.countUnit || undefined, original?.countUnit || undefined );
		set( 'smallStop', ms( this.smallStop ), ms( original?.smallStop ) );
		set( 'idleTimeout', ms( this.idleTimeout ), ms( original?.idleTimeout ) );
		set( 'width', this.width, create ? undefined : original.width );
		return Object.keys( args ).length ? [new Mutation( this.type, this.id, args, create ? MutationType.Create : MutationType.Update )] : [];
	}
	get properties():OeeMachine{ return new OeeMachine( this ); }
	histGroupId?:number;
	group?:HistRef;
	calendarId?:number;
	idealCycle?:number;//milliseconds a unit
	idealInvalid:boolean;//the form's ideal is no number above zero:  never saved
	countUnit?:string;
	smallStop:MsField;
	idleTimeout:MsField;
	width:number;
	processed?:Date;
	static typeName = 'OeeMachine';
}
//a QL row, with the group as it joins it, or an OeeMachine, with the fields the form holds
export type OeeMachineProps = Partial<SlugRowProps> & { histGroupId?:number|string|null; histGroup?:any; group?:HistRef; calendarId?:number|string|null; idealCycle?:number|string|null; idealInvalid?:boolean; countUnit?:string|null; smallStop?:MsField; idleTimeout?:MsField; width?:number|string|null; processed?:Date|string|null };

export const oeeCategories = ['Unplanned', 'Planned', 'Excluded'];
//A stop reason, one list per gateway:  its category and the code the Reason signal reports for it, none for one only operators pick.
export class OeeReason extends SlugRow<OeeReason>{
	constructor( obj:OeeReasonProps ){
		super( OeeReason.typeName, <SlugRowProps>obj );
		this.category = obj.category ?? undefined;
		this.code = obj.code;
	}
	override get canSave():boolean{ return super.canSave && oeeCategories.includes( this.category ?? '' ) && !this.fieldError( 'code' ); }
	override fieldError( field:string ):string|undefined{ return field=='code' && Number.isNaN( this.codeValue ) ? "The Reason signal's value for the reason:  a whole number." : undefined; }
	//the code as a number, none for a blank, NaN for text that is no whole number
	get codeValue():number|undefined{
		if( this.code==null || String( this.code ).trim()==='' )
			return undefined;
		const n = Number( this.code );
		return Number.isInteger( n ) ? n : NaN;
	}
	override equals( row:ISlugRow ):boolean{
		const o = <OeeReason>row;
		return super.equals( row ) && this.category==o.category && Object.is( this.codeValue, o.codeValue );
	}
	override mutation( original:OeeReason ):Mutation[]{
		console.assert( this.canSave );
		const args:Record<string,unknown> = super.mutationArgs( original );
		const create = !original?.id;
		if( this.category!=original?.category )
			args['category'] = this.category;
		if( create ? this.codeValue!=undefined : this.codeValue!=original.codeValue )
			args['code'] = this.codeValue ?? null;
		return Object.keys( args ).length ? [new Mutation( this.type, this.id, args, create ? MutationType.Create : MutationType.Update )] : [];
	}
	get properties():OeeReason{ return new OeeReason( this ); }
	category?:string;
	code?:number|string|null;
	static typeName = 'OeeReason';
}
export type OeeReasonProps = Partial<SlugRowProps> & { category?:string|null; code?:number|string|null };

//A machine's signal:  a member of its group in a role, one row per role.  `running` is a State's values that mean running, a json
//list, none for [true];  `ratePeriod` a Rate's time unit in milliseconds.
export const oeeRoles = ['State', 'Total', 'Rate', 'Good', 'Reject', 'Reason'] as const;
export type OeeRole = typeof oeeRoles[number];
export type OeeSignal = { id:number; role:OeeRole; nodeId:number; running?:string; ratePeriod:number };
export function toOeeSignal( json:any ):OeeSignal{
	return { id: Number( json.id ), role: <OeeRole>String( json.role ), nodeId: Number( json.histGroupNodeId ), running: json.running || undefined, ratePeriod: Number( json.ratePeriod ?? msPerMinute ) };
}
//What a State's running values are typed as, `true` or `2, 3`, as the column's json list:  null for a blank, the default [true],
//and none for text that is no list of Booleans, numbers and strings.
export function parseOeeRunning( text:string ):string|null|undefined{
	const t = text.trim();
	if( !t )
		return null;
	try{
		const list = JSON.parse( t.startsWith( '[' ) ? t : `[${t}]` );
		return Array.isArray( list ) && list.length && list.every( v=>['boolean', 'number', 'string'].includes( typeof v ) ) ? JSON.stringify( list ) : undefined;
	}
	catch{
		return undefined;
	}
}
//the column's list as it is typed:  without its brackets
export function oeeRunningText( running:string|undefined ):string{
	try{
		const list = running ? JSON.parse( running ) : [true];
		return Array.isArray( list ) ? list.map( v=>JSON.stringify( v ) ).join( ', ' ) : running ?? '';
	}
	catch{
		return running ?? '';
	}
}

//What a machine's or a signal's mutation answers beside its row (OeeMAwait):  the warnings the page shows - a counter's deviation
//band, a member held past `settle` - and `recompute`, an engine input changed, so the rows before now were made with the old one.
//`data` is the mutation's answer by its name, a create's a list of the row and an update's the object.
export type OeeMutationResult = { id?:number; warnings:string[]; recompute:boolean };
export function toOeeMutationResult( data:any ):OeeMutationResult{
	const answer = data && typeof data=='object' ? Object.values( data )[0] : undefined;
	const row:any = Array.isArray( answer ) ? answer[0] : answer;
	const id = Number( row?.id );
	return { id: Number.isFinite( id ) && id>0 ? id : undefined, warnings: Array.isArray( row?.warnings ) ? row.warnings.map( String ) : [], recompute: row?.recompute===true };
}

//oeeMachineStatus( machine ) (oee.jsonnet):  whether the gateway runs OEE, the machine's calendar as the gateway's client finds it,
//the end of the last row its engine wrote, and the engine - live, starting, waiting, blocked with `why`, or deleted.
export type OeeMachineStatus = { enabled:boolean; calendar:string; processed?:Date; engine:string; why?:string; state?:string };
export function toOeeMachineStatus( json:any ):OeeMachineStatus|undefined{
	return json ? { enabled: json.enabled===true, calendar: String( json.calendar ?? 'none' ), processed: json.processed ? new Date( json.processed ) : undefined, engine: String( json.engine ?? '' ), why: json.why || undefined, state: json.state || undefined } : undefined;
}
//the status in a line, as the machine's page says it
export function oeeStatusText( s:OeeMachineStatus ):string{
	if( !s.enabled )
		return "OEE isn't running on this gateway:  it has no oee block, or no historian.";
	const engine = s.engine=='live' ? `The engine is live${s.state ? `, the machine ${s.state}` : ''}.`
		: s.engine=='blocked' ? `The engine can't run:  ${s.why ? s.why.replace( /\.?$/, '.' ) : 'the gateway gives no reason.'}`
		: s.engine=='deleted' ? 'The machine is deleted:  no engine runs.'
		: `The engine is ${s.engine}.`;
	const processed = s.processed ? `  Rows are written to ${s.processed.toLocaleString()}.` : '';
	const calendar = s.calendar=='missing' ? "  Its calendar isn't among the application server's, so it is planned around the clock." : '';
	return `${engine}${processed}${calendar}`;
}