import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MAT_TABS_CONFIG } from '@angular/material/tabs';
import { AppService, CalendarService, Mutation, Properties } from 'jde-framework';
import { ExNodeId } from '../../../model/ex-node-id';
import { histThresholdBlank } from '../../../model/hist';
import { OeeMachine } from '../../../model/oee-admin';
import { HistAdminService } from '../../../services/hist-admin-service';
import { OeeAdminService } from '../../../services/oee-admin-service';
import { OeeMachineDetail } from './oee-machine-detail';
import { flush } from 'jde-spa/testing';
import { configureDetailPage } from 'jde-framework/testing';
import { Pumps } from 'jde-opc/testing';

const row = { id: 4, name: 'Pump 1', slug: 'pump1', histGroup: {id: 5, name: 'Pumps'}, calendarId: 2, idealCycle: 1000, smallStop: 300000, idleTimeout: null, width: 60000 };
const member = ( id:number, name:string )=>({ id, node: new ExNodeId( {i: 6000+id, nsu: Pumps} ), templateNodeId: undefined, thresholds: {...histThresholdBlank}, attributes: {displayName: name} });
//The page over stubs:  `mutations` reads what Save sent the gateway, whose `mutate` answers `saved` or throws it, and the admin
//service records what the Signals tab asks and sends.  `render` draws the page, where the rest drive it by hand.  `registrations`
//is what the application server lists, a hub by default;  `open` moves the page to another row, as the router does.
const create = async ( pageRow:any, {saved, render=false, registrations=[{instanceName: 'g', programName: 'OpcHub'}]}:{saved?:unknown; render?:boolean; registrations?:{instanceName:string; programName:string}[]}={} )=>{
	const gateway = { slug: 'g', mutate: vi.fn( async ( _m:Mutation[]|string )=>{ if( saved instanceof Error ) throw saved; return saved; } ) };
	const mutations = ()=>gateway.mutate.mock.calls.flatMap( ([m])=>Array.isArray( m ) ? m.map( x=>x.toString()! ) : [m] );
	const admin = {
		groups: vi.fn( async ()=>[{id: 5, name: 'Pumps', slug: 'pumps'}, {id: 6, name: 'Fans', slug: 'fans'}] ),
		signals: vi.fn( async ()=>[{id: 9, role: 'State', nodeId: 7, running: undefined, ratePeriod: 60000}] ),
		status: vi.fn( async ()=>({enabled: true, calendar: 'found', engine: 'live', state: 'running'}) ),
		createSignal: vi.fn( async ( _g:unknown, _machine:number, role:string, _node:number )=>{
			if( role=='Rate' )
				throw new Error( 'A count is one Total or one Rate, not both.' );
			return {id: 10, warnings: ['Member 8 has an ExceptionDeviation of 5:  a band holds a counter\'s record back.'], recompute: false};
		} ),
		updateSignal: vi.fn( async ()=>({warnings: ['running changed'], recompute: true}) ),
		purgeSignal: vi.fn( async ()=>{} ),
		recompute: vi.fn( async ( _g:unknown, _id:number, start:Date, end:Date )=>({start, end}) )
	};
	const hist = { groupMembers: vi.fn( async ()=>[member( 7, 'status' ), member( 8, 'totalCount' ), member( 9, 'motorRpm' )] ) };
	const app = { registrations: vi.fn( async ()=>registrations ) };
	const {snackbar, router, data} = configureDetailPage( pageRow, {gateway, providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },//the page is driven by hand:  the fixture's own first change detection would run ngOnInit again
		{ provide: OeeAdminService, useValue: admin },
		{ provide: HistAdminService, useValue: hist },
		{ provide: CalendarService, useValue: {calendars: async ()=>[{id: 2, name: 'Plant', slug: 'plant', timeZone: 'UTC', dayStart: 360}]} },
		{ provide: AppService, useValue: app },
		{ provide: MAT_TABS_CONFIG, useValue: {animationDuration: '0ms'} }
	]} );
	TestBed.overrideComponent( Properties, {set: {template: '', imports: []}} );//the form reads a whole schema
	const fixture = TestBed.createComponent( OeeMachineDetail );
	const page = fixture.componentInstance;
	if( render )
		fixture.detectChanges();
	else
		page.ngOnInit();
	await flush( 3 );
	const open = async ( next:any, nextGateway:unknown=gateway )=>{
		data.next( {pageData: {row: next, routing: {}, schema: {enums: new Map()}, gateway: nextGateway}} );
		await flush( 3 );
	};
	return { page, fixture, mutations, admin, hist, snackbar, router, gateway, open };
};
const rate = { id: 11, role: 'Rate' as const, nodeId: 9, running: undefined, ratePeriod: 60000 };

describe( 'OeeMachineDetail', ()=>{
	it( 'saves a new machine over its group, the ideal typed as a rate stored as the cycle', async ()=>{
		const d = await create( {} );
		expect( d.page.isNew ).toBe( true );
		expect( d.page.groups() ).toHaveLength( 2 );
		d.page.properties.set( new OeeMachine( {...d.page.properties(), name: 'Pump 1', slug: 'pump1'} ) );
		expect( d.page.machine.canSave ).toBe( false );
		d.page.setGroup( 5 );
		expect( d.page.machine.canSave ).toBe( true );
		d.page.setIdealMode( 'rate' );
		d.page.setIdeal( '-' );
		expect( d.page.machine.canSave ).toBe( false );
		expect( d.page.idealHint() ).toContain( 'above zero' );
		d.page.setIdealMode( 'cycle' );//the stored ideal, none, shown again:  the refused text goes with it
		expect( d.page.machine.canSave ).toBe( true );
		expect( d.page.idealHint() ).toBe( 'Blank measures no performance.' );
		d.page.setIdealMode( 'rate' );
		d.page.setIdeal( '120' );
		expect( d.page.idealText() ).toBe( '120' );
		expect( d.page.idealHint() ).toBe( 'A cycle of 0.5 s' );
		d.page.setIdealMode( 'cycle' );
		expect( d.page.idealText() ).toBe( '0.5' );
		d.page.setWidth( 5 );
		d.page.setCalendar( 2 );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['createOeeMachine( slug:"pump1", name:"Pump 1", histGroupId:5, calendarId:2, idealCycle:500, smallStop:300000, width:300000 )'] );
		expect( d.router.navigate ).toHaveBeenCalled();
		expect( d.admin.signals ).not.toHaveBeenCalled();//nothing to pick until it is saved
	} );
	it( "shows a refusal as the form's error, until the form is edited", async ()=>{
		const d = await create( {}, {saved: new Error( "Group 'Pumps' already has a machine, 'Pump 1':  a group has one." )} );
		d.page.properties.set( new OeeMachine( {...d.page.properties(), name: 'Pump 2', slug: 'pump2'} ) );
		d.page.setGroup( 5 );
		d.page.tabIndex.set( 1 );
		await d.page.onSubmitClick();
		expect( d.page.saveError() ).toBe( "Group 'Pumps' already has a machine, 'Pump 1':  a group has one." );
		expect( d.page.tabIndex() ).toBe( 0 );
		expect( d.router.navigate ).not.toHaveBeenCalled();
		expect( d.snackbar.exception ).not.toHaveBeenCalled();
		d.page.setGroup( 6 );
		expect( d.page.saveError() ).toBeUndefined();
	} );
	it( 'says the refusal under the form', async ()=>{
		const d = await create( row, {saved: new Error( 'calendarId 3 is no calendar the gateway holds.' ), render: true} );
		d.page.setCalendar( 3 );
		await d.page.onSubmitClick();
		d.fixture.detectChanges();
		expect( d.fixture.nativeElement.querySelector( '.refused[role=alert]' ).textContent ).toContain( 'calendarId 3 is no calendar the gateway holds.' );
	} );
	it( "warns of what a save's answer warns of", async ()=>{
		const d = await create( row, {saved: [{updateOeeMachine: {recompute: true, warnings: ['idleTimeout changed:  the rows written before now were made with the old one.']}}]} );
		d.page.properties.set( Object.assign( new OeeMachine( d.page.properties() ), {idleTimeout: '90000'} ) );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['updateOeeMachine( id:4, idleTimeout:90000 )'] );
		expect( d.snackbar.warn ).toHaveBeenCalledWith( expect.stringContaining( 'idleTimeout changed' ) );
		expect( d.router.navigate ).toHaveBeenCalled();
	} );
	it( "reads the signals over the group's members, and links the group and the calendar", async ()=>{
		const d = await create( row );
		expect( d.admin.signals ).toHaveBeenCalledWith( expect.anything(), 4 );
		expect( d.hist.groupMembers ).toHaveBeenCalledWith( expect.anything(), 5 );
		expect( d.page.signalOf( 'State' )?.nodeId ).toBe( 7 );
		expect( d.page.signalOf( 'Total' ) ).toBeUndefined();
		expect( d.page.members().map( m=>d.page.memberLabel( m ) ) ).toEqual( ['status', 'totalCount', 'motorRpm'] );
		expect( d.page.statusText() ).toBe( 'The engine is live, the machine running.' );
		expect( d.page.groupLink() ).toEqual( ['/apps/gateways', 'g', 'groups', 'pumps'] );
		expect( d.page.calendarLink() ).toEqual( ['/apps/gateways', 'g', 'calendars', 'plant'] );//a hub's calendars are a tab of its own page
		expect( d.page.missingCalendar() ).toBeUndefined();
		d.page.setCalendar( 3 );
		expect( d.page.missingCalendar() ).toBe( 3 );
		expect( d.page.calendarLink() ).toBeUndefined();
	} );
	it( "links a stand-alone gateway's calendar on its application server's page", async ()=>{
		const d = await create( row, {registrations: [{instanceName: 'g', programName: 'OpcGateway'}, {instanceName: 'g', programName: 'OpcHub'}, {instanceName: 'app', programName: 'AppServer'}]} );
		expect( d.page.calendarLink() ).toEqual( ['/apps/appServers', 'app', 'calendars', 'plant'] );//an OpcGateway under the slug:  no hub, as GatewayDetail.isHub
	} );
	it( 'sends a pick at once, with its warnings beside the role', async ()=>{
		const d = await create( row );
		await d.page.setSignal( 'Total', 8 );
		expect( d.admin.createSignal ).toHaveBeenCalledWith( expect.anything(), 4, 'Total', 8 );
		expect( d.page.warnings()['Total'] ).toEqual( [expect.stringContaining( 'ExceptionDeviation' )] );
		expect( d.admin.signals ).toHaveBeenCalledTimes( 2 );//read again after
		expect( d.hist.groupMembers ).toHaveBeenCalledTimes( 1 );//which leaves the members be
		await d.page.setSignal( 'State', 9 );//the role has a signal:  its member changes
		expect( d.admin.updateSignal ).toHaveBeenLastCalledWith( expect.anything(), 9, {histGroupNodeId: 9} );
		await d.page.setSignal( 'State', 7 );//its own:  nothing sent
		expect( d.admin.updateSignal ).toHaveBeenCalledTimes( 1 );
		await d.page.setSignal( 'State', 0 );
		expect( d.admin.purgeSignal ).toHaveBeenCalledWith( expect.anything(), 9 );
	} );
	it( 'says a refused pick on the tab, and puts the select back', async ()=>{
		const d = await create( row );
		const select = { writeValue: vi.fn() };
		await d.page.setSignal( 'Rate', 9, select );
		expect( d.page.signals.error() ).toBe( 'Rate:  A count is one Total or one Rate, not both.' );
		expect( select.writeValue ).toHaveBeenCalledWith( 0 );
		expect( d.page.signals.busy() ).toBe( false );
	} );
	it( 'puts a refused running list and rate period back', async ()=>{
		const d = await create( row );
		const state = d.page.signalOf( 'State' )!, input = { value: '2, 3' }, select = { writeValue: vi.fn() };
		d.admin.updateSignal.mockRejectedValueOnce( new Error( 'refused' ) );
		await d.page.setRunning( state, input.value, input );
		expect( d.page.signals.error() ).toBe( 'State:  refused' );
		expect( input.value ).toBe( d.page.runningText( state ) );
		d.admin.updateSignal.mockRejectedValueOnce( new Error( 'refused' ) );
		await d.page.setRatePeriod( rate, 3600000, select );
		expect( select.writeValue ).toHaveBeenCalledWith( 60000 );
	} );
	it( "offers a Rate's own period beside the three", async ()=>{
		const d = await create( row );
		expect( d.page.ratePeriodsOf( rate ).map( p=>p.label ) ).toEqual( ['A second', 'A minute', 'An hour'] );
		expect( d.page.ratePeriodsOf( {...rate, ratePeriod: 10000} ).map( p=>p.label ) ).toEqual( ['A second', '10 seconds', 'A minute', 'An hour'] );
		expect( d.page.ratePeriodsOf( {...rate, ratePeriod: 1500} ).map( p=>p.ms ) ).toEqual( [1000, 1500, 60000, 3600000] );
	} );
	it( "drops an edit's answer once the page shows another machine", async ()=>{
		const d = await create( row );
		const answer:{settle:( value:any )=>void} = { settle: ()=>{} }, select = { writeValue: vi.fn() };
		d.admin.createSignal.mockImplementationOnce( ()=>new Promise( r=>answer.settle = r ) );
		const pick = d.page.setSignal( 'Total', 8 );
		await d.open( {...row, id: 5, slug: 'fan1', histGroup: {id: 6, name: 'Fans'}} );
		const reads = d.admin.signals.mock.calls.length;
		answer.settle( {id: 10, warnings: ['for pump1'], recompute: true} );
		await pick;
		expect( d.page.warnings()['Total'] ).toBeUndefined();
		expect( d.page.recompute() ).toBe( false );
		expect( d.admin.signals ).toHaveBeenCalledTimes( reads );
		d.admin.createSignal.mockImplementationOnce( ()=>new Promise( ( _r, reject )=>answer.settle = reject ) );
		const refused = d.page.setSignal( 'Total', 8, select );
		await d.open( row );
		answer.settle( new Error( 'refused' ) );
		await refused;
		expect( d.page.signals.error() ).toBeUndefined();
		expect( select.writeValue ).not.toHaveBeenCalled();
	} );
	it( "clears the last machine's range, and the last gateway's lists", async ()=>{
		const d = await create( row );
		d.page.recomputeFrom.set( '2026-11-12T06:00' );
		await d.open( {...row, id: 5, slug: 'fan1'} );
		expect( d.page.recomputeFrom() ).toBe( '' );
		expect( d.page.groups() ).toHaveLength( 2 );//the same gateway's, read again
		d.admin.groups.mockRejectedValueOnce( new Error( 'down' ) );
		await d.open( {...row, id: 6}, {...d.gateway, slug: 'h'} );
		expect( d.page.groups() ).toEqual( [] );
		expect( d.page.groupLink() ).toBeUndefined();//never g's group under h
	} );
	describe( 'Recompute', ()=>{
		afterEach( ()=>vi.useRealTimers() );
		it( 'takes a blank To as now when it is pressed, not when From was typed', async ()=>{
			const d = await create( row );
			vi.useFakeTimers( {toFake: ['Date']} );
			vi.setSystemTime( new Date( '2026-11-12T10:00' ) );
			d.page.recomputeFrom.set( '2026-11-12T10:01' );
			expect( d.page.recomputeRange() ).toBeDefined();//a minute ahead:  pressed now, it is refused
			await d.page.recomputeRows();
			expect( d.admin.recompute ).not.toHaveBeenCalled();
			expect( d.page.signals.error() ).toContain( 'From is after now' );
			vi.setSystemTime( new Date( '2026-11-12T10:30' ) );
			await d.page.recomputeRows();
			expect( d.admin.recompute ).toHaveBeenCalledWith( expect.anything(), 4, new Date( '2026-11-12T10:01' ), new Date( '2026-11-12T10:30' ) );
		} );
	} );
	it( 'offers the recompute when an engine input changes, and queues the range', async ()=>{
		const d = await create( row );
		const state = d.page.signalOf( 'State' )!;
		await d.page.setRunning( state, 'Run' );
		expect( d.page.signals.error() ).toContain( 'no list of values' );
		expect( d.admin.updateSignal ).not.toHaveBeenCalled();
		await d.page.setRunning( state, 'true' );
		expect( d.admin.updateSignal ).not.toHaveBeenCalled();//the default, as it was
		await d.page.setRunning( state, '2, 3' );
		expect( d.admin.updateSignal ).toHaveBeenCalledWith( expect.anything(), 9, {running: '[2,3]'} );
		expect( d.page.recompute() ).toBe( true );
		expect( d.page.recomputeRange() ).toBeUndefined();//no start
		d.page.recomputeFrom.set( '2026-11-12T06:00' );
		d.page.recomputeTo.set( '2026-11-12T05:00' );
		expect( d.page.recomputeRange() ).toBeUndefined();//ends before it starts
		d.page.recomputeTo.set( '2026-11-12T07:00' );
		await d.page.recomputeRows();
		expect( d.admin.recompute ).toHaveBeenCalledWith( expect.anything(), 4, new Date( '2026-11-12T06:00' ), new Date( '2026-11-12T07:00' ) );
		expect( d.page.recompute() ).toBe( false );
		expect( d.snackbar.info ).toHaveBeenCalledWith( expect.stringContaining( 'Recompute queued' ) );
	} );
	it( 'draws a role a row on Signals', async ()=>{
		const d = await create( row, {render: true} );
		d.page.tabIndex.set( 1 );
		d.fixture.detectChanges( false );
		await flush( 3 );
		d.fixture.detectChanges( false );
		expect( [...d.fixture.nativeElement.querySelectorAll( '.signal .name' )].map( ( e:Element )=>e.textContent ) ).toEqual( ['State', 'Total', 'Rate', 'Good', 'Reject', 'Reason'] );
		expect( d.fixture.nativeElement.querySelector( '.status' ).textContent ).toContain( 'The engine is live' );
	} );
} );