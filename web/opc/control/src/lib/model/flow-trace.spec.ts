import Long from 'long';
import { FlowGraph, FlowVersion } from './flow';
import { flowMessageId, FlowTraceRecord, toFlowCountRow, toFlowTraceRecord } from './flow-trace';
import { flowCountStep, flowDeployMarks, flowGraphCounts, flowJsonText, flowLineagePath, flowOwnRecords, FlowPlayer, flowRecordSummary, flowVersionAt } from './flow-trace-view';

const t = ( s:string )=>new Date( `2026-11-14T${s}Z` );
const node = ( id:string, type:string, wires:string[][]=[], name='' )=>({ id, type, name, x: 0, y: 0, props: {}, wires });
//an inject into a switch of two rules, the first to a function then a log, the second to a debug
const graph:FlowGraph = { nodes: [ node( 'in', 'inject', [['sw']] ), node( 'sw', 'switch', [['fn'], ['dbg']], 'Route' ), node( 'fn', 'function', [['log', 'dbg']] ), node( 'log', 'log' ), node( 'dbg', 'debug' ) ] };
const version = ( n:number, deployed?:string ):FlowVersion=>({ id: n, version: n, graph, identityId: 1, userName: 'ann', deployed: deployed ? t( deployed ) : undefined });
const record = ( time:string, node:string, msgId:string|undefined, more:Partial<FlowTraceRecord>={} ):FlowTraceRecord=>({ time: t( time ), msgId, runId: 0, node, kind: 'hop', ports: [], cut: false, blocks: [], ...more });

describe( 'flow-trace', ()=>{
	//the gateway writes a 64-bit integer as the web's Long, and the reads take an id as text
	it( 'reads a message id from a Long, its json, a number or text', ()=>{
		expect( flowMessageId( Long.fromString( '7205759403792793601', true ) ) ).toBe( '7205759403792793601' );
		expect( flowMessageId( {low: 1, high: 1677721600, unsigned: true} ) ).toBe( '7205759403792793601' );
		expect( flowMessageId( 42 ) ).toBe( '42' );
		expect( flowMessageId( '42' ) ).toBe( '42' );
		expect( [flowMessageId( 0 ), flowMessageId( null ), flowMessageId( 'x' ), flowMessageId( 1.5 )] ).toEqual( [undefined, undefined, undefined, undefined] );
	} );
	it( 'reads a record and a count row as the gateway answers them', ()=>{
		const r = toFlowTraceRecord( {time: '2026-11-14T08:00:00.250Z', msgId: {low: 5, high: 0, unsigned: true}, parentId: null, runId: 0, node: 'fn', kind: 'clone', ports: [0, 1], json: {payload: 3, _id: {low: 5, high: 0, unsigned: true}}, cut: false, error: null, status: null, blocks: []} );
		expect( r ).toMatchObject( {time: t( '08:00:00.250' ), msgId: '5', parentId: undefined, node: 'fn', kind: 'clone', ports: [0, 1], cut: false, error: undefined, status: undefined} );
		expect( flowJsonText( r.json ) ).toBe( '{\n\t"payload": 3,\n\t"_id": {\n\t\t"low": 5,\n\t\t"high": 0,\n\t\t"unsigned": true\n\t}\n}' );
		const s = toFlowTraceRecord( {time: '2026-11-14T08:00:01Z', msgId: null, runId: 3, node: 'fn', kind: 'status', ports: [], json: null, cut: false, error: null, status: {fill: 'red', shape: 'dot', text: 'down'}, blocks: ['a']} );
		expect( s ).toMatchObject( {msgId: undefined, runId: 3, kind: 'status', status: {fill: 'red', shape: 'dot', text: 'down'}, blocks: ['a']} );
		expect( s.json ).toBeUndefined();//the gateway's null, a message the node passed on unchanged:  no json to show
		expect( flowRecordSummary( s ) ).toBe( 'down' );
		expect( flowRecordSummary( record( '08:00:00', 'fn', '1', {error: 'boom'} ) ) ).toBe( 'boom' );
		expect( flowRecordSummary( record( '08:00:00', 'fn', '1', {json: {payload: {a: [1, 2]}}} ) ) ).toBe( '{ "a": [ 1, 2 ] }' );
		expect( flowRecordSummary( record( '08:00:00', 'fn', '1', {json: 'cut text', cut: true} ) ) ).toBe( 'cut text' );
		expect( toFlowCountRow( {time: '2026-11-14T08:00:00Z', node: 'sw', received: 4, sent: [3, 1], drops: 0, errors: 1} ) ).toEqual( {time: t( '08:00:00' ), node: 'sw', counts: {received: 4, sent: [3, 1], drops: 0, errors: 1}} );
	} );
	it( 'picks a bucket that keeps a range within two hundred', ()=>{
		expect( flowCountStep( t( '08:00:00' ), t( '09:00:00' ) ).by ).toBe( 'PT1M' );
		expect( flowCountStep( t( '00:00:00' ), new Date( '2026-11-15T00:00:00Z' ) ).by ).toBe( 'PT15M' );
		expect( flowCountStep( new Date( '2026-10-14T00:00:00Z' ), new Date( '2026-11-14T00:00:00Z' ) ).by ).toBe( 'PT6H' );
		expect( flowCountStep( new Date( '2016-11-14T00:00:00Z' ), new Date( '2026-11-14T00:00:00Z' ) ).by ).toBe( 'P7D' );
	} );
	//each node's figures summed over the buckets, and a wire's its port's `sent` under its node
	it( 'puts the counts onto the wires', ()=>{
		const rows = [
			toFlowCountRow( {time: '2026-11-14T08:00:00Z', node: 'sw', received: 4, sent: [3, 1], drops: 0, errors: 0} ),
			toFlowCountRow( {time: '2026-11-14T08:01:00Z', node: 'sw', received: 2, sent: [2], drops: 1, errors: 0} ),
			toFlowCountRow( {time: '2026-11-14T08:01:00Z', node: 'fn', received: 5, sent: [5], drops: 0, errors: 2} ),
			toFlowCountRow( {time: '2026-11-14T08:01:00Z', node: 'gone', received: 1, sent: [1], drops: 0, errors: 0} )
		];
		const counts = flowGraphCounts( rows, graph );
		expect( counts.nodes.get( 'sw' ) ).toEqual( {received: 6, sent: [5, 1], drops: 1, errors: 0} );
		expect( [...counts.wires] ).toEqual( [['sw:0>fn', 5], ['sw:1>dbg', 1], ['fn:0>log', 5], ['fn:0>dbg', 5]] );
	} );
	//the scrubber's mapping of a time to the version in force:  the last deployed at or before it
	it( 'finds the version in force at a time, and the deploys along a range', ()=>{
		const versions = [version( 3, '10:00:00' ), version( 2, '08:30:00' ), version( 1, '08:00:00' )];
		expect( flowVersionAt( versions, t( '09:00:00' ) )?.version ).toBe( 2 );
		expect( flowVersionAt( versions, t( '08:30:00' ) )?.version ).toBe( 2 );
		expect( flowVersionAt( versions, t( '12:00:00' ) )?.version ).toBe( 3 );
		expect( flowVersionAt( versions, t( '07:00:00' ) )?.version ).toBe( 1 );//nothing ran before the first:  the first is drawn
		expect( flowVersionAt( [version( 2 ), version( 1 )], t( '07:00:00' ) )?.version ).toBe( 1 );//undated, the lowest
		expect( flowVersionAt( [], t( '07:00:00' ) ) ).toBeUndefined();
		expect( flowDeployMarks( versions, t( '08:00:00' ), t( '10:00:00' ) ).map( m=>[m.version.version, m.at] ) ).toEqual( [[1, 0], [2, 0.25], [3, 1]] );
		expect( flowDeployMarks( versions, t( '08:10:00' ), t( '09:00:00' ) ).map( m=>m.version.version ) ).toEqual( [2] );
	} );
	//the lineage in time order:  the message's own records light its nodes and the wires between them;  a clone's first record
	//follows its parent's at the node it was made, so its path starts on that node's wire
	it( "lights a message's path and its kin's from the lineage", ()=>{
		const lineage = [
			record( '08:00:00.000', 'in', '1', {kind: 'birth', ports: [0]} ),
			record( '08:00:00.010', 'sw', '1', {ports: [0]} ),
			record( '08:00:00.020', 'fn', '1', {ports: [0]} ),
			record( '08:00:00.021', 'fn', '2', {kind: 'clone', parentId: '1'} ),
			record( '08:00:00.030', 'log', '1' ),
			record( '08:00:00.031', 'dbg', '2' ),
			record( '08:00:00.040', 'sw', undefined, {kind: 'status', status: {fill: 'green', shape: 'dot', text: 'ok'}} )
		];
		const own = flowLineagePath( lineage, '1', graph );
		expect( own.own ).toEqual( {nodes: ['in', 'sw', 'fn', 'log'], wires: ['in:0>sw', 'sw:0>fn', 'fn:0>log']} );
		expect( own.kin ).toEqual( {nodes: ['dbg'], wires: ['fn:0>dbg']} );
		const clone = flowLineagePath( lineage, '2', graph );
		expect( clone.own ).toEqual( {nodes: ['fn', 'dbg'], wires: ['fn:0>dbg']} );
		expect( clone.kin.nodes ).toEqual( ['in', 'sw', 'log'] );
		expect( flowOwnRecords( lineage, '2' ).map( r=>r.node ) ).toEqual( ['fn', 'dbg'] );
		//two outputs wired to one node:  the wire of the port the record sent on
		const both:FlowGraph = { nodes: [node( 'sw', 'switch', [['dbg'], ['dbg']] ), node( 'dbg', 'debug' )] };
		expect( flowLineagePath( [record( '08:00:00', 'sw', '3', {ports: [1]} ), record( '08:00:01', 'dbg', '3' )], '3', both ).own.wires ).toEqual( ['sw:1>dbg'] );
		expect( flowLineagePath( [record( '08:00:00', 'sw', '3' ), record( '08:00:01', 'dbg', '3' )], '3', both ).own.wires ).toEqual( ['sw:0>dbg'] );//no port said:  the first
	} );
	//Play on a fake clock:  the player moves speed times the wall clock's step, fires the records it passes, waits at the end of
	//a page while more are to come, and stops at the range's end
	it( 'steps the records at speed on a clock of its own', ()=>{
		const player = new FlowPlayer( t( '08:00:00' ), t( '08:00:10' ) );
		const page1 = [record( '08:00:01', 'in', '1' ), record( '08:00:02', 'sw', '1' ), record( '08:00:05', 'fn', '1' )];
		player.add( page1, false );
		player.speed = 10;
		player.play( 1000 );
		expect( player.playing ).toBe( true );
		expect( player.tick( 1050 ).map( r=>r.node ) ).toEqual( [] );//50 ms at 10x:  half a second in, nothing yet
		expect( player.at ).toBe( t( '08:00:00.500' ).getTime() );
		expect( player.tick( 1100 ).map( r=>r.node ) ).toEqual( ['in'] );//a record at the time reached fires
		expect( player.at ).toBe( t( '08:00:01' ).getTime() );
		expect( player.tick( 2000 ).map( r=>r.node ) ).toEqual( ['sw', 'fn'] );//to 08:00:10 by the clock, held at the last record while a page is to come
		expect( player.at ).toBe( t( '08:00:05' ).getTime() );
		expect( [player.held, player.holds( t( '08:00:05' ).getTime() ), player.holds( t( '08:00:06' ).getTime() )] ).toEqual( [0, true, false] );
		player.add( [record( '08:00:07', 'log', '1' )], true );
		expect( player.tick( 2100 ).map( r=>r.node ) ).toEqual( [] );//a second on from where it stood
		expect( [player.at, player.playing, player.done, player.holds( t( '08:00:10' ).getTime() )] ).toEqual( [t( '08:00:06' ).getTime(), true, false, true] );
		expect( player.tick( 3000 ).map( r=>r.node ) ).toEqual( ['log'] );//to the range's end, and no further
		expect( [player.at, player.playing, player.done] ).toEqual( [t( '08:00:10' ).getTime(), false, true] );
		expect( player.tick( 3100 ) ).toEqual( [] );//paused by its end
		player.play( 4000 );//from the start again
		expect( player.at ).toBe( t( '08:00:00' ).getTime() );
		player.speed = 1;
		expect( player.tick( 5500 ).map( r=>r.node ) ).toEqual( ['in'] );
		player.seek( t( '08:00:04' ).getTime() );
		expect( player.tick( 6500 ).map( r=>r.node ) ).toEqual( ['fn'] );
		player.pause();
		expect( player.tick( 7500 ) ).toEqual( [] );
	} );
	//a player put to play at a time:  its records begin there, and it holds no time before it, nor past what it has read
	it( 'plays from where it was put', ()=>{
		const player = new FlowPlayer( t( '08:00:00' ), t( '08:00:10' ), t( '08:00:04' ).getTime() );
		expect( [player.from, player.at] ).toEqual( [t( '08:00:04' ).getTime(), t( '08:00:04' ).getTime()] );
		expect( player.holds( t( '08:00:04' ).getTime() ) ).toBe( false );//nothing read yet
		player.add( [record( '08:00:04', 'sw', '1' ), record( '08:00:05', 'fn', '1' )], false );
		expect( [t( '08:00:03' ), t( '08:00:04' ), t( '08:00:05' ), t( '08:00:06' )].map( d=>player.holds( d.getTime() ) ) ).toEqual( [false, true, true, false] );
		player.seek( t( '08:00:01' ).getTime() );
		expect( player.at ).toBe( t( '08:00:04' ).getTime() );//no further back than its records
		player.play( 0 );
		expect( player.tick( 1000 ).map( r=>r.node ) ).toEqual( ['sw', 'fn'] );
		player.add( [], true );
		expect( player.holds( t( '08:00:10' ).getTime() ) ).toBe( true );
	} );
} );