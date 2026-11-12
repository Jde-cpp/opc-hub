import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MAT_TABS_CONFIG } from '@angular/material/tabs';
import { AppService, CalendarService, Mutation, Properties } from 'jde-framework';
import { OeeLine, OeeLineMember, OeePlant } from '../../../model/oee-lines';
import { OeeAdminService } from '../../../services/oee-admin-service';
import { OeeService } from '../../../services/oee-service';
import { OeeLineDetail } from './oee-line-detail';
import { flush } from 'jde-spa/testing';
import { configureDetailPage } from 'jde-framework/testing';

const row = { id: 11, name: 'Line A', slug: 'line-a', level: 'Line', calendarId: 2 };
const line = ( id:number, name:string, level:string )=>({ id, name, slug: name.toLowerCase().replace( ' ', '-' ), level });
const member = ( id:number, lineId:number, of:{machine?:number; line?:number}, sort:number ):OeeLineMember=>({ id, lineId, machineId: of.machine, memberLineId: of.line, sort });
//the gateway's plant:  Line A of a cell and two pumps, in an area, beside a second line
const plantOf = ():OeePlant=>({
	machines: [1, 2, 3, 4].map( id=>({ id, name: `Pump ${id}`, slug: `pump${id}` }) ),
	lines: [line( 10, 'Packaging', 'Area' ), line( 11, 'Line A', 'Line' ), line( 12, 'Line B', 'Line' ), line( 13, 'Cell', 'Cell' )],
	members: [member( 1, 10, {line: 11}, 0 ), member( 2, 11, {machine: 1}, 0 ), member( 3, 11, {line: 13}, 0 ), member( 4, 11, {machine: 2}, 5 ), member( 5, 12, {machine: 3}, 0 )]
});
//The page over stubs:  `mutations` reads what Save sent the gateway, whose `mutate` throws `refusal` when there is one;  the
//admin service records each edit of the members, `refuse` making the next throw, and the plant is read again after each.
const create = async ( pageRow:any, {refusal, render=false}:{refusal?:string; render?:boolean}={} )=>{
	const gateway = { slug: 'g', mutate: vi.fn( async ( _m:Mutation[]|string )=>{ if( refusal ) throw new Error( refusal ); } ) };
	const mutations = ()=>gateway.mutate.mock.calls.flatMap( ([m])=>Array.isArray( m ) ? m.map( x=>x.toString()! ) : [m] );
	const answers = { plant: plantOf(), refuse: <string|undefined>undefined };
	const edit = ()=>vi.fn( async ( ..._args:unknown[] )=>{
		if( answers.refuse )
			throw new Error( answers.refuse );
	} );
	const admin = { addLineMember: edit(), sortLineMember: edit(), purgeLineMember: edit() };
	const service = { plant: vi.fn( async ( _g:unknown, _strict:boolean )=>answers.plant ) };
	const {snackbar, router, data} = configureDetailPage( pageRow, {gateway, providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },
		{ provide: OeeAdminService, useValue: admin },
		{ provide: OeeService, useValue: service },
		{ provide: CalendarService, useValue: {calendars: async ()=>[{id: 2, name: 'Plant', slug: 'plant', timeZone: 'UTC', dayStart: 360}]} },
		{ provide: AppService, useValue: {registrations: async ()=>[{instanceName: 'g', programName: 'OpcHub'}]} },
		{ provide: MAT_TABS_CONFIG, useValue: {animationDuration: '0ms'} }
	]} );
	TestBed.overrideComponent( Properties, {set: {template: '', imports: []}} );//the form reads a whole schema
	const fixture = TestBed.createComponent( OeeLineDetail );
	const page = fixture.componentInstance;
	if( render )
		fixture.detectChanges();
	else
		page.ngOnInit();
	await flush( 3 );
	const open = async ( next:any )=>{
		data.next( {pageData: {row: next, routing: {}, schema: {enums: new Map()}, gateway}} );
		await flush( 3 );
	};
	return { page, fixture, mutations, admin, service, answers, snackbar, router, gateway, open };
};

describe( 'OeeLineDetail', ()=>{
	it( 'saves a new line with its level and calendar, and reads no members until it is saved', async ()=>{
		const d = await create( {} );
		expect( d.page.picks['level'].map( p=>p.value ) ).toEqual( ['Site', 'Area', 'Line', 'Cell'] );
		d.page.properties.set( Properties.edited( OeeLine, d.page.properties(), {name: 'Packaging', slug: 'packaging', level: 'Area'} ) );
		d.page.setCalendar( 2 );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['createOeeLine( slug:"packaging", name:"Packaging", level:"Area", calendarId:2 )'] );
		expect( d.service.plant ).not.toHaveBeenCalled();
		expect( d.router.navigate ).toHaveBeenCalled();
	} );
	it( "shows a refusal as the form's error, on Properties, until the form is edited", async ()=>{
		const refusal = "Line 'Line A' is in 'Packaging', an Area:  a member's level is no higher than its parent's.";
		const d = await create( row, {refusal, render: true} );
		d.page.tabIndex.set( 1 );
		d.page.properties.set( Properties.edited( OeeLine, d.page.properties(), {level: 'Site'} ) );
		await d.page.onSubmitClick();
		d.fixture.detectChanges();
		expect( d.page.tabIndex() ).toBe( 0 );
		expect( d.fixture.nativeElement.querySelector( '.refused[role=alert]' ).textContent ).toContain( refusal );
		expect( d.snackbar.exception ).not.toHaveBeenCalled();
		d.page.setCalendar( 0 );
		expect( [d.page.saveError(), d.page.line.calendarId] ).toEqual( [undefined, undefined] );
	} );
	it( "links the line's calendar, and shows one the application server's list lacks by its id", async ()=>{
		const d = await create( row );
		const calendar = ()=>[d.page.calendars.link( d.page.line.calendarId ), d.page.calendars.missing( d.page.line.calendarId )];
		expect( calendar() ).toEqual( [['/apps/gateways', 'g', 'calendars', 'plant'], undefined] );
		await d.open( {...row, calendarId: 7} );
		expect( calendar() ).toEqual( [undefined, 7] );
	} );
	it( 'lists the members in their order, each with its page, and one whose machine is deleted by its id', async ()=>{
		const d = await create( row );
		expect( d.service.plant ).toHaveBeenCalledWith( d.gateway, true );
		expect( d.page.rows().map( r=>[r.name, r.kind, r.link?.join( '/' ), r.first, r.last] ) ).toEqual( [
			['Pump 1', 'Machine', '/apps/gateways/g/machines/pump1', true, false], ['Cell', 'Cell', '/apps/gateways/g/lines/cell', false, false], ['Pump 2', 'Machine', '/apps/gateways/g/machines/pump2', false, true]] );
		d.answers.plant = { ...plantOf(), machines: plantOf().machines.filter( m=>m.id!=2 ) };
		await d.page.refresh();
		expect( d.page.rows().at( -1 ) ).toMatchObject( {name: 'Machine 2, deleted', link: undefined} );
	} );
	it( 'offers the picker the machines it lacks and the lines its level may hold, and adds a pick after the members there are', async ()=>{
		const d = await create( row );
		expect( [d.page.candidates().machines.map( m=>m.name ), d.page.candidates().lines.map( l=>l.name )] ).toEqual( [['Pump 3', 'Pump 4'], ['Line B']] );//no area, not its own cell
		const select = { writeValue: vi.fn() };
		await d.page.add( 'm4', select );
		expect( d.admin.addLineMember ).toHaveBeenLastCalledWith( d.gateway, 11, {machine: 4}, 6 );
		expect( select.writeValue ).toHaveBeenCalledWith( null );
		await d.page.add( 'l12' );
		expect( d.admin.addLineMember ).toHaveBeenLastCalledWith( d.gateway, 11, {line: 12}, 6 );
		expect( d.service.plant ).toHaveBeenCalledTimes( 3 );//read again after each
	} );
	it( "moves a member by making the members' sorts their places, and removes one by its purge", async ()=>{
		const d = await create( row );
		await d.page.move( d.page.rows()[2], -1 );//Pump 2 above the cell:  Pump 1 0, Pump 2 1, Cell 2
		expect( d.admin.sortLineMember.mock.calls.map( c=>c.slice( 1 ) ) ).toEqual( [[4, 1], [3, 2]] );
		await d.page.move( d.page.rows()[0], -1 );//the first has nowhere to go
		expect( d.admin.sortLineMember ).toHaveBeenCalledTimes( 2 );
		await d.page.remove( d.page.rows()[1] );
		expect( d.admin.purgeLineMember ).toHaveBeenCalledWith( d.gateway, 3 );
	} );
	it( "sends a move's sorts together", async ()=>{
		const d = await create( row );
		const answer:(()=>void)[] = [];
		d.admin.sortLineMember.mockImplementation( ()=>new Promise<void>( r=>answer.push( r ) ) );
		const moved = d.page.move( d.page.rows()[2], -1 );
		await flush( 1 );
		expect( d.admin.sortLineMember ).toHaveBeenCalledTimes( 2 );//both under way before either is answered
		answer.forEach( r=>r() );
		await moved;
		expect( d.page.members.busy() ).toBe( false );
	} );
	it( "says the gateway's refusal of an edit on the tab, after the read that follows it", async ()=>{
		const d = await create( row );
		d.answers.refuse = "Machine 'Pump 3' is reached twice from 'Packaging'.";
		await d.page.add( 'm3' );
		expect( d.page.members.error() ).toBe( "Add:  Machine 'Pump 3' is reached twice from 'Packaging'." );
		expect( [d.page.members.busy(), d.service.plant.mock.calls.length] ).toEqual( [false, 2] );
		d.answers.refuse = undefined;
		await d.page.remove( d.page.rows()[0] );
		expect( d.page.members.error() ).toBeUndefined();
	} );
} );