import { Alarms, conditionIdField, conditionSelect, EConditionField, isRefreshEnd, isRefreshStart, sortAlarms, stateText, toAlarmRow } from './alarm';
import { EventFields } from './event';
import { NodeId } from './node-id';

const overspeed = new NodeId( {ns: 2, i: 5020} ), other = new NodeId( {ns: 2, i: 5021} );
//a condition event as the stream delivers one asked with conditionSelect:  the eight base fields, then the thirteen condition fields
type State = { active?:boolean; acked?:boolean; confirmed?:boolean; retain?:boolean; comment?:string; branch?:NodeId; texts?:boolean };
const condition = ( id:number, ms:number, conditionId:NodeId=overspeed, {active=true, acked=false, confirmed=false, retain=true, comment='', branch, texts=true}:State={} ):EventFields=><EventFields>[
	Uint8Array.from( [id] ), new NodeId( {i: 2915} ), new NodeId( {ns: 2, i: 5005} ), 'Pump (Manual)',
	{seconds: Math.floor( ms/1000 ), nanos: ms%1000*1_000_000}, {seconds: Math.floor( ms/1000 ), nanos: ms%1000*1_000_000}, 'too fast', 700,
	conditionId, 'overspeed', branch, retain,
	texts ? 'Enabled' : undefined, true, texts ? ( active ? 'Active' : 'Inactive' ) : undefined, active, texts ? ( acked ? 'Acknowledged' : 'Unacknowledged' ) : undefined, acked,
	texts ? ( confirmed ? 'Confirmed' : 'Unconfirmed' ) : undefined, confirmed, comment
];
const bracket = ( type:number, ms:number ):EventFields=><EventFields>[Uint8Array.from( [type] ), new NodeId( {i: type} ), new NodeId( {i: 2253} ), 'Server', {seconds: ms/1000, nanos: 0}, {seconds: ms/1000, nanos: 0}, undefined, 100];
const plain = ( ms:number ):EventFields=><EventFields>[Uint8Array.from( [9] ), new NodeId( {i: 2041} ), new NodeId( {ns: 2, i: 5005} ), 'Pump (Manual)', {seconds: ms/1000, nanos: 0}, {seconds: ms/1000, nanos: 0}, 'hello', 500];

describe( 'conditionSelect', ()=>{
	it( 'asks the ConditionId first, under the operand\'s own spelling, then each state with its Id, in the field order', ()=>{
		expect( conditionSelect[0] ).toBe( conditionIdField );
		expect( conditionIdField ).toBe( 'i=2782#NodeId' );
		expect( conditionSelect[EConditionField.AckedId-8] ).toBe( 'AckedState/Id' );
		expect( conditionSelect[EConditionField.Comment-8] ).toBe( 'Comment' );
		expect( conditionSelect ).toHaveLength( 13 );
	} );
} );
describe( 'toAlarmRow', ()=>{
	it( 'reads the condition fields out of an event, keyed by the ConditionId, the states by their Ids with the server\'s texts', ()=>{
		const row = toAlarmRow( condition( 1, 1_700_000_000_000, overspeed, {comment: 'seen', acked: true} ) )!;
		expect( row.key ).toBe( 'ns=2;i=5020' );
		expect( row.eventKey ).toBe( '01' );
		expect( row.conditionId.equals( overspeed ) ).toBe( true );
		expect( row.time?.getTime() ).toBe( 1_700_000_000_000 );
		expect( [row.source, row.name, row.branch, row.message, row.comment, row.severity] ).toEqual( ['Pump (Manual)', 'overspeed', '', 'too fast', 'seen', 700] );
		expect( [row.enabled, row.active, row.acked, row.confirmed, row.retain] ).toEqual( [true, true, true, false, true] );
		expect( [row.enabledText, row.activeText, row.ackedText, row.confirmedText] ).toEqual( ['Enabled', 'Active', 'Acknowledged', 'Unconfirmed'] );
	} );
	it( 'spells a state from its Id where the server gave no text, and keys a branch under its condition', ()=>{
		const row = toAlarmRow( condition( 1, 1_700_000_000_000, overspeed, {texts: false, acked: true, branch: new NodeId( {ns: 2, i: 7} )} ) )!;
		expect( [row.enabledText, row.activeText, row.ackedText, row.confirmedText] ).toEqual( ['Enabled', 'Active', 'Acknowledged', 'Unconfirmed'] );
		expect( row.branch ).toBe( 'ns=2;i=7' );
		expect( row.key ).toBe( 'ns=2;i=5020/ns=2;i=7' );
		expect( stateText( undefined, undefined, 'Active', 'Inactive' ) ).toBe( '' );
	} );
	it( 'is nothing for an event that is no condition\'s, and keeps a condition whose Retain the server left out', ()=>{
		expect( toAlarmRow( plain( 1_700_000_000_000 ) ) ).toBeUndefined();
		expect( toAlarmRow( bracket( 2787, 1_700_000_000_000 ) ) ).toBeUndefined();
		expect( toAlarmRow( bracket( 2789, 1_700_000_000_000 ) ) ).toBeUndefined();
		expect( toAlarmRow( bracket( 3035, 1_700_000_000_000 ) ) ).toBeUndefined();
		const fields = condition( 1, 1_700_000_000_000 );
		fields[EConditionField.Retain] = undefined;
		expect( toAlarmRow( fields )?.retain ).toBe( true );
	} );
} );
describe( 'Alarms', ()=>{
	it( 'keeps one row per condition, its latest event by Time, and ignores an older one or a repeat of the same EventId', ()=>{
		const a = new Alarms();
		expect( a.apply( condition( 1, 1_700_000_000_000 ) ) ).toBe( 'rows' );
		expect( a.apply( condition( 2, 1_700_000_002_000, overspeed, {acked: true, comment: 'seen'} ) ) ).toBe( 'rows' );
		expect( a.rows ).toHaveLength( 1 );
		expect( a.rows[0].eventKey ).toBe( '02' );
		expect( a.rows[0].ackedText ).toBe( 'Acknowledged' );
		expect( a.apply( condition( 1, 1_700_000_000_000 ) ) ).toBeUndefined();//the raise again, a replay of what the acknowledge superseded
		expect( a.apply( condition( 2, 1_700_000_002_000, overspeed, {acked: true, comment: 'seen'} ) ) ).toBeUndefined();//the same transition again
		expect( a.rows[0].eventKey ).toBe( '02' );
		expect( a.apply( condition( 3, 1_700_000_001_000, other ) ) ).toBe( 'rows' );
		expect( a.rows.map( r=>r.key ) ).toEqual( ['ns=2;i=5020', 'ns=2;i=5021'] );
		expect( a.apply( plain( 1_700_000_003_000 ) ) ).toBeUndefined();
	} );
	it( 'drops a condition once an event says Retain is false, and shows none that arrives so', ()=>{
		const a = new Alarms();
		a.apply( condition( 1, 1_700_000_000_000 ) );
		expect( a.apply( condition( 2, 1_700_000_001_000, overspeed, {active: false, acked: true, confirmed: true, retain: false} ) ) ).toBe( 'rows' );
		expect( a.rows ).toEqual( [] );
		expect( a.apply( condition( 3, 1_700_000_002_000, other, {retain: false} ) ) ).toBeUndefined();
		expect( a.rows ).toEqual( [] );
	} );
	it( 'brackets a refresh:  RefreshStart marks the rows stale, the replay keeps its own, and RefreshEnd drops the rest', ()=>{
		const a = new Alarms();
		a.apply( condition( 1, 1_700_000_000_000 ) );
		a.apply( condition( 2, 1_700_000_001_000, other ) );
		expect( isRefreshStart( bracket( 2787, 1_700_000_002_000 ) ) ).toBe( true );
		expect( a.apply( bracket( 2787, 1_700_000_002_000 ) ) ).toBe( 'refresh' );
		expect( a.refreshing ).toBe( true );
		expect( a.rows ).toHaveLength( 2 );
		expect( a.apply( condition( 1, 1_700_000_000_000 ) ) ).toBeUndefined();//overspeed replayed, EventId and Time kept
		expect( isRefreshEnd( bracket( 2788, 1_700_000_003_000 ) ) ).toBe( true );
		expect( a.apply( bracket( 2788, 1_700_000_003_000 ) ) ).toBe( 'rows' );
		expect( a.refreshing ).toBe( false );
		expect( a.rows.map( r=>r.key ) ).toEqual( ['ns=2;i=5020'] );//the other, not replayed, is retained no more
		a.apply( bracket( 2787, 1_700_000_004_000 ) );
		expect( a.apply( condition( 4, 1_700_000_004_500, overspeed, {acked: true} ) ) ).toBe( 'rows' );//a live transition between keeps its row too
		expect( a.apply( bracket( 2788, 1_700_000_005_000 ) ) ).toBe( 'refresh' );
		expect( a.rows.map( r=>r.eventKey ) ).toEqual( ['04'] );
	} );
	it( 'shows a refusal on the row until its next event, and clear empties it', ()=>{
		const a = new Alarms();
		a.apply( condition( 1, 1_700_000_000_000 ) );
		a.problem( 'ns=2;i=5020', 'Acknowledge refused' );
		expect( a.rows[0].problem ).toBe( 'Acknowledge refused' );
		a.apply( condition( 2, 1_700_000_001_000 ) );
		expect( a.rows[0].problem ).toBeUndefined();
		a.apply( bracket( 2787, 1_700_000_002_000 ) );
		a.clear();
		expect( a.rows ).toEqual( [] );
		expect( a.refreshing ).toBe( false );
	} );
} );
describe( 'sortAlarms', ()=>{
	it( 'orders newest first, the receive time standing in for a missing Time, then by key', ()=>{
		const a = toAlarmRow( condition( 1, 1_700_000_000_000 ) )!, b = toAlarmRow( condition( 2, 1_700_000_001_000, other ) )!;
		const fields = condition( 3, 0, new NodeId( {ns: 2, i: 5022} ) );
		fields[4] = undefined;
		const c = toAlarmRow( fields, new Date( 1_700_000_000_500 ) )!;
		expect( sortAlarms( [a, b, c] ).map( r=>r.eventKey ) ).toEqual( ['02', '03', '01'] );
	} );
} );