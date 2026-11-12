import { cycleToRate, oeeIdealText, OeeMachine, OeeReason, oeeRunningText, oeeStatusText, parseOeeIdeal, parseOeeRunning, rateToCycle, toOeeMachineStatus, toOeeMutationResult, toOeeSignal } from './oee-admin';

describe( 'the ideal as a cycle or a rate', ()=>{
	it( 'turns a rate a minute into the cycle, and back', ()=>{
		expect( rateToCycle( 60 ) ).toBe( 1000 );
		expect( rateToCycle( 7 ) ).toBeCloseTo( 8571.428571, 5 );
		expect( cycleToRate( 1000 ) ).toBe( 60 );
		expect( cycleToRate( rateToCycle( 7 )! ) ).toBeCloseTo( 7, 9 );
		expect( rateToCycle( 0 ) ).toBeUndefined();
		expect( cycleToRate( -1 ) ).toBeUndefined();
		expect( cycleToRate( NaN ) ).toBeUndefined();
	} );
	it( 'reads what is typed as the cycle it stores', ()=>{
		expect( parseOeeIdeal( '1.5', 'cycle' ) ).toBe( 1500 );//seconds a unit
		expect( parseOeeIdeal( '120', 'rate' ) ).toBe( 500 );
		expect( parseOeeIdeal( ' ', 'rate' ) ).toBeUndefined();//blank measures no performance
		expect( parseOeeIdeal( '0', 'cycle' ) ).toBeNaN();
		expect( parseOeeIdeal( 'fast', 'rate' ) ).toBeNaN();
	} );
	it( 'shows a cycle in either form, a rate typed reading the same again', ()=>{
		expect( oeeIdealText( 1500, 'cycle' ) ).toBe( '1.5' );
		expect( oeeIdealText( 500, 'rate' ) ).toBe( '120' );
		expect( oeeIdealText( parseOeeIdeal( '7', 'rate' ), 'rate' ) ).toBe( '7' );
		expect( oeeIdealText( parseOeeIdeal( '7', 'rate' ), 'cycle' ) ).toBe( '8.57143' );
		expect( oeeIdealText( undefined, 'cycle' ) ).toBe( '' );
	} );
} );

describe( 'OeeMachine', ()=>{
	const saved = new OeeMachine( {id: 4, name: 'Pump 1', slug: 'pump1', histGroup: {id: 5, name: 'Pumps'}, calendarId: 2, idealCycle: 1000, countUnit: 'rev', smallStop: 300000, idleTimeout: null, width: 60000} );
	it( 'creates with its group and each column set', ()=>{
		const m = new OeeMachine( {name: 'Pump 1', slug: ' pump1 '} );
		expect( m.canSave ).toBe( false );//no group
		m.histGroupId = 5;
		m.idealCycle = 500;
		expect( m.canSave ).toBe( true );
		expect( m.mutation( new OeeMachine( {} ) ).map( x=>x.toString() ) ).toEqual( ['createOeeMachine( slug:"pump1", name:"Pump 1", histGroupId:5, idealCycle:500, smallStop:300000, width:60000 )'] );
	} );
	it( 'updates what changed, null for a column cleared, and never the group', ()=>{
		expect( saved.group ).toEqual( {id: 5, name: 'Pumps'} );
		expect( saved.histGroupId ).toBe( 5 );
		const m = Object.assign( saved.properties, {smallStop: '60000', idleTimeout: '90000', countUnit: ''} );//as Properties writes a field:  its text
		m.calendarId = undefined;
		m.width = 300000;
		expect( m.equals( saved ) ).toBe( false );
		expect( m.mutation( saved ).map( x=>x.toString() ) ).toEqual( ['updateOeeMachine( id:4, calendarId:null, countUnit:null, smallStop:60000, idleTimeout:90000, width:300000 )'] );
		expect( saved.properties.equals( saved ) ).toBe( true );
		expect( saved.properties.mutation( saved ) ).toEqual( [] );
	} );
	it( "doesn't save a threshold that is no whole number, or an ideal that is no number", ()=>{
		const m = saved.properties;
		m.smallStop = '';
		expect( m.fieldError( 'smallStop' ) ).toContain( 'Required' );
		expect( m.canSave ).toBe( false );
		m.smallStop = 0;
		m.idleTimeout = '1.5';
		expect( m.fieldError( 'idleTimeout' ) ).toContain( 'whole number' );
		expect( m.canSave ).toBe( false );
		m.idleTimeout = '';
		expect( m.canSave ).toBe( true );
		m.idealInvalid = true;
		expect( m.canSave ).toBe( false );
	} );
} );

describe( 'OeeReason', ()=>{
	it( 'needs a category, and a code that is a whole number or blank', ()=>{
		const r = new OeeReason( {name: 'Jam', slug: 'jam'} );
		expect( r.canSave ).toBe( false );
		r.category = 'Unplanned';
		expect( r.canSave ).toBe( true );
		expect( r.mutation( new OeeReason( {} ) ).map( x=>x.toString() ) ).toEqual( ['createOeeReason( slug:"jam", name:"Jam", category:"Unplanned" )'] );
		r.code = '7';
		expect( r.mutation( new OeeReason( {} ) ).map( x=>x.toString() ) ).toEqual( ['createOeeReason( slug:"jam", name:"Jam", category:"Unplanned", code:7 )'] );
		r.code = '7.5';
		expect( r.fieldError( 'code' ) ).toContain( 'whole number' );
		expect( r.canSave ).toBe( false );
	} );
	it( 'updates the category and the code changed, null for a code cleared', ()=>{
		const saved = new OeeReason( {id: 3, name: 'Jam', slug: 'jam', category: 'Unplanned', code: 7} );
		const r = Object.assign( saved.properties, {category: 'Planned', code: ''} );
		expect( r.equals( saved ) ).toBe( false );
		expect( r.mutation( saved ).map( x=>x.toString() ) ).toEqual( ['updateOeeReason( id:3, category:"Planned", code:null )'] );
		expect( Object.assign( saved.properties, {code: '7'} ).equals( saved ) ).toBe( true );
	} );
} );

describe( "a machine's signals", ()=>{
	it( 'reads a row', ()=>{
		expect( toOeeSignal( {id: 9, role: 'State', histGroupNodeId: 7, running: '[2,3]', ratePeriod: 60000} ) ).toEqual( {id: 9, role: 'State', nodeId: 7, running: '[2,3]', ratePeriod: 60000} );
		expect( toOeeSignal( {id: 10, role: 'Total', histGroupNodeId: 8, running: null} ) ).toEqual( {id: 10, role: 'Total', nodeId: 8, running: undefined, ratePeriod: 60000} );
	} );
	it( 'takes the running values as typed, and shows them without brackets', ()=>{
		expect( parseOeeRunning( 'true' ) ).toBe( '[true]' );
		expect( parseOeeRunning( '2, 3' ) ).toBe( '[2,3]' );
		expect( parseOeeRunning( '["Run", "Jog"]' ) ).toBe( '["Run","Jog"]' );
		expect( parseOeeRunning( '' ) ).toBeNull();//the default
		expect( parseOeeRunning( 'Run' ) ).toBeUndefined();
		expect( parseOeeRunning( '[]' ) ).toBeUndefined();
		expect( parseOeeRunning( '{"a":1}' ) ).toBeUndefined();
		expect( oeeRunningText( undefined ) ).toBe( 'true' );
		expect( oeeRunningText( '[2,3]' ) ).toBe( '2, 3' );
		expect( oeeRunningText( '["Run"]' ) ).toBe( '"Run"' );
	} );
	it( "reads a mutation's answer:  a create's list, an update's object, or a row count", ()=>{
		expect( toOeeMutationResult( {createOeeMachineSignal: [{id: 9, warnings: ['a band']}]} ) ).toEqual( {id: 9, warnings: ['a band'], recompute: false} );
		expect( toOeeMutationResult( {updateOeeMachineSignal: {recompute: true, warnings: ['running changed']}} ) ).toEqual( {id: undefined, warnings: ['running changed'], recompute: true} );
		expect( toOeeMutationResult( {updateOeeMachine: 1} ) ).toEqual( {id: undefined, warnings: [], recompute: false} );
		expect( toOeeMutationResult( null ) ).toEqual( {id: undefined, warnings: [], recompute: false} );
	} );
	it( 'says how the engine stands', ()=>{
		expect( toOeeMachineStatus( null ) ).toBeUndefined();
		const live = toOeeMachineStatus( {enabled: true, calendar: 'missing', processed: '2026-11-12T06:00:00Z', engine: 'live', why: null, state: 'running'} )!;
		expect( oeeStatusText( live ) ).toContain( 'The engine is live, the machine running.' );
		expect( oeeStatusText( live ) ).toContain( "isn't among the application server's" );
		expect( oeeStatusText( {enabled: true, calendar: 'none', engine: 'blocked', why: 'it has no signals yet'} ) ).toBe( "The engine can't run:  it has no signals yet." );
		expect( oeeStatusText( {enabled: false, calendar: 'none', engine: 'waiting'} ) ).toContain( "isn't running on this gateway" );
	} );
} );