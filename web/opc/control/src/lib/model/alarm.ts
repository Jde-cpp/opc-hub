import { EBaseField, eventFieldText, EventFields, eventTime } from './event';
import { NodeId } from './node-id';
import { hexOf, Value } from './value';

//The Alarms view's fields beside the base eight (clients spec *Alarms and conditions*, #426), in the order the view asks them:  the
//ConditionId first, under the operand's own spelling, `i=2782#NodeId` - ConditionType's NodeId attribute, which no browse path says
//(EventClause) - then the condition's name and branch, Retain, each state's text with its Id, and the comment.
export const conditionIdField = 'i=2782#NodeId';
export const conditionSelect:readonly string[] = [conditionIdField, 'ConditionName', 'BranchId', 'Retain', 'EnabledState', 'EnabledState/Id', 'ActiveState', 'ActiveState/Id', 'AckedState', 'AckedState/Id', 'ConfirmedState', 'ConfirmedState/Id', 'Comment'];
export const enum EConditionField{ ConditionId=8, ConditionName, BranchId, Retain, Enabled, EnabledId, Active, ActiveId, Acked, AckedId, Confirmed, ConfirmedId, Comment }
//RefreshStartEventType and RefreshEndEventType, the Server object's bracket around a ConditionRefresh's replay (Part 9 5.5.7)
export const refreshStartType = new NodeId( {i: 2787} ), refreshEndType = new NodeId( {i: 2788} );
//RefreshRequiredEventType, a server telling its clients to call ConditionRefresh (Part 9), and EventQueueOverflowEventType, which it
//puts in an item's queue once it has had to discard events (Part 4 5.12.1.5).
export const refreshRequiredType = new NodeId( {i: 2789} ), queueOverflowType = new NodeId( {i: 3035} );
//AcknowledgeableConditionType's Acknowledge and Confirm, ConditionType's AddComment:  called on the condition node with the EventId and a comment
export const acknowledgeMethod = new NodeId( {i: 9111} ), confirmMethod = new NodeId( {i: 9113} ), addCommentMethod = new NodeId( {i: 9029} );
export const badEventIdUnknown = 0x809A0000;//what those answer for an EventId that is not the condition's latest

//A condition as the view shows it:  its latest event, the condition fields read out.  `key` is the ConditionId with the branch, what
//the view keeps one row per;  `eventKey` the EventId as hex, which a replay repeats;  `eventId` the bytes an Acknowledge takes.  A
//state is its Id where the server gave one, and its text as the server spells it, or the Id's reading where it did not.  `problem`
//is the last refusal of an action on the row, which its next event clears.
export type AlarmRow = {
	key:string; eventKey:string;
	conditionId:NodeId; eventId:Value|undefined;
	time:Date|undefined; received:Date;
	source:string; name:string; branch:string;
	retain:boolean; enabled:boolean|undefined; active:boolean|undefined; acked:boolean|undefined; confirmed:boolean|undefined;
	enabledText:string; activeText:string; ackedText:string; confirmedText:string;
	severity:number|undefined; message:string; comment:string;
	fields:EventFields;
	problem?:string;
};
export function conditionOf( fields:EventFields ):NodeId|undefined{
	const id = fields[EConditionField.ConditionId];
	return id instanceof NodeId ? id : undefined;
}
function eventTypeIs( fields:EventFields, type:NodeId ):boolean{
	const t = fields[EBaseField.EventType];
	return t instanceof NodeId && type.equals( t );
}
export function isRefreshStart( fields:EventFields ):boolean{ return eventTypeIs( fields, refreshStartType ); }
export function isRefreshEnd( fields:EventFields ):boolean{ return eventTypeIs( fields, refreshEndType ); }
export function isRefreshRequired( fields:EventFields ):boolean{ return eventTypeIs( fields, refreshRequiredType ); }
export function isQueueOverflow( fields:EventFields ):boolean{ return eventTypeIs( fields, queueOverflowType ); }
function booleanOf( v:Value|undefined ):boolean|undefined{ return typeof v=='boolean' ? v : undefined; }
//a two-state's text:  the server's where it gave one, else the Id read as `on`/`off`, else nothing
export function stateText( text:Value|undefined, id:boolean|undefined, on:string, off:string ):string{
	const t = eventFieldText( text );
	return t ? t : id===undefined ? "" : id ? on : off;
}
//An event's row, or nothing for an event that is no condition's - a plain event, the refresh bracket
export function toAlarmRow( fields:EventFields, received:Date=new Date() ):AlarmRow|undefined{
	const conditionId = conditionOf( fields );
	if( !conditionId )
		return undefined;
	const branchId = fields[EConditionField.BranchId];
	const branch = branchId instanceof NodeId && !( branchId.ns==0 && branchId.id===0 ) ? branchId.uaString() : "";
	const eventId = fields[EBaseField.EventId];
	const enabled = booleanOf( fields[EConditionField.EnabledId] ), active = booleanOf( fields[EConditionField.ActiveId] ), acked = booleanOf( fields[EConditionField.AckedId] ), confirmed = booleanOf( fields[EConditionField.ConfirmedId] );
	const severity = fields[EBaseField.Severity];
	return {
		key: branch ? `${conditionId.uaString()}/${branch}` : conditionId.uaString(),
		eventKey: eventId instanceof Uint8Array ? hexOf( eventId ) : typeof eventId=='string' ? eventId : "",
		conditionId, eventId, time: eventTime( fields[EBaseField.Time] ), received,
		source: eventFieldText( fields[EBaseField.SourceName] ), name: eventFieldText( fields[EConditionField.ConditionName] ), branch,
		retain: booleanOf( fields[EConditionField.Retain] ) ?? true,//a server leaving Retain out keeps the condition:  only a false removes it
		enabled, active, acked, confirmed,
		enabledText: stateText( fields[EConditionField.Enabled], enabled, "Enabled", "Disabled" ),
		activeText: stateText( fields[EConditionField.Active], active, "Active", "Inactive" ),
		ackedText: stateText( fields[EConditionField.Acked], acked, "Acknowledged", "Unacknowledged" ),
		confirmedText: stateText( fields[EConditionField.Confirmed], confirmed, "Confirmed", "Unconfirmed" ),
		severity: typeof severity=='number' ? severity : undefined,
		message: eventFieldText( fields[EBaseField.Message] ), comment: eventFieldText( fields[EConditionField.Comment] ),
		fields
	};
}
//The Events tab's stream kept by ConditionId (clients spec *Alarms and conditions*):  one row per condition and branch, its latest
//event by Time - an older one, a replay of a transition already shown, changes nothing - gone when an event says Retain is false,
//since a condition inactive, acknowledged and confirmed is of no more interest.  The refresh bracket:  RefreshStart marks every row
//held as stale, each condition replayed or raised between un-marks its own, and RefreshEnd drops what stayed stale, the conditions the
//server no longer retains.  A lower notifier's item gets no bracket, so there a row goes only on its own Retain.
export class Alarms{
	rows:AlarmRow[] = [];
	refreshing = false;
	#stale = new Set<string>();
	//an event into the table:  'rows' when the table changed, 'refresh' when the bracket alone moved, nothing for an event that changed neither
	apply( fields:EventFields, received:Date=new Date() ):'rows'|'refresh'|undefined{
		if( isRefreshStart( fields ) ){
			this.refreshing = true;
			this.#stale = new Set( this.rows.map( r=>r.key ) );
			return 'refresh';
		}
		if( isRefreshEnd( fields ) ){
			this.refreshing = false;
			const stale = this.#stale;
			this.#stale = new Set();
			const kept = this.rows.filter( r=>!stale.has( r.key ) );
			const changed = kept.length!=this.rows.length;
			this.rows = kept;
			return changed ? 'rows' : 'refresh';
		}
		const row = toAlarmRow( fields, received );
		if( !row )
			return undefined;
		this.#stale.delete( row.key );
		const i = this.rows.findIndex( r=>r.key==row.key );
		const held = i>=0 ? this.rows[i] : undefined;
		if( held && ( held.eventKey && held.eventKey==row.eventKey || held.time && row.time && row.time<held.time ) )
			return undefined;
		if( !row.retain ){
			if( !held )
				return undefined;
			this.rows = this.rows.filter( r=>r.key!=row.key );
			return 'rows';
		}
		this.rows = held ? this.rows.map( r=>r===held ? row : r ) : [...this.rows, row];
		return 'rows';
	}
	//a refusal of an action on the row, shown with it until its next event
	problem( key:string, text:string|undefined ):void{
		this.rows = this.rows.map( r=>r.key==key ? {...r, problem: text} : r );
	}
	clear():void{
		this.rows = [];
		this.refreshing = false;
		this.#stale = new Set();
	}
}
//newest first by the transition's time, the receive time standing in, then by key for a stable order
export function sortAlarms( rows:AlarmRow[] ):AlarmRow[]{
	const time = ( r:AlarmRow )=>( r.time ?? r.received ).getTime();
	return [...rows].sort( ( a, b )=>time( b )-time( a ) || a.key.localeCompare( b.key ) );
}