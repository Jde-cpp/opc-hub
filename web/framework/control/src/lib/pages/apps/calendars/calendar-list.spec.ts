import { Component, input } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { ProfileStore, RouteStore } from 'jde-spa';
import { AppService } from '../../../services/app/app-service';
import { QLListData } from '../../../services/ql-list-resolver';
import { CellText } from '../../graphql/table/graphql-table';
import { QLList } from '../../ql/list/ql-list';
import { CalendarList } from './calendar-list';
import { profileStoreStub, stable } from 'jde-spa/testing';
import { qlAnswering, required, scalar, slugSchema } from 'jde-framework/testing';

@Component( {selector: 'ql-list', template: ''} )
class ListStub{ listData = input<QLListData>(); ql = input<unknown>(); linkPrefix = input<string[]>(); cellText = input<CellText>(); }

//#396:  the Calendars tab resolves its own list over the application server, as the gateway's historian tabs do theirs.
describe( 'CalendarList', ()=>{
	afterEach( ()=>localStorage.clear() );
	const rows = [{id: 3, name: 'Plant', slug: 'plant', timeZone: 'America/New_York', dayStart: 360}, {id: 4, name: 'Line 2', slug: 'line2', timeZone: 'Europe/Berlin', dayStart: 0}];
	const create = async ( schemaWithEnums:()=>Promise<unknown> = async ()=>slugSchema( 'Calendar', required( 'timeZone' ), required( 'dayStart', 'UInt' ), scalar( 'description' ), scalar( 'deleted', 'DateTime' ) ) )=>{
		const app = { schemaWithEnums: vi.fn( schemaWithEnums ), query: qlAnswering( {calendars: rows} ) };
		TestBed.configureTestingModule({ providers: [
			{ provide: AppService, useValue: app },
			{ provide: ProfileStore, useValue: profileStoreStub() }
		]});
		TestBed.overrideComponent( CalendarList, {remove: {imports: [QLList]}, add: {imports: [ListStub]}} );
		const fixture = TestBed.createComponent( CalendarList );
		fixture.componentRef.setInput( 'url', 'apps/appServers/main' );
		await stable( fixture );
		const list = ():ListStub|undefined=>fixture.debugElement.query( By.directive( ListStub ) )?.componentInstance;
		return { fixture, app, list };
	};

	it( "lists the application server's calendars, their rows under the page's url for a calendar's page to find", async ()=>{
		const { app, list } = await create();
		expect( app.schemaWithEnums ).toHaveBeenCalledWith( 'Calendar' );
		const data = list()!.listData()!;
		expect( data.results['calendars'].map( ( r:{slug:string} )=>r.slug ) ).toEqual( ['plant', 'line2'] );
		expect( data.profile.current().fields.filter( f=>f.displayed ).map( f=>f.name ) ).toEqual( ['name', 'timeZone', 'dayStart', 'slug', 'description'] );
		expect( app.query.mock.calls.at( -1 ) ).toEqual( [expect.stringContaining( 'deleted:$deleted' ), {deleted: null}] );//the live rows
		expect( list()!.ql() ).toBe( app );
		expect( list()!.linkPrefix() ).toEqual( ['calendars'] );//a row opens calendars/<slug> under the page
		expect( TestBed.inject( RouteStore ).getChildren( 'apps/appServers/main/calendars' ).map( c=>[c.path, c.title] ) ).toEqual( [['plant', 'Plant'], ['line2', 'Line 2']] );
	} );
	it( 'shows the day start as a time of day', async ()=>{
		const text = (await create()).list()!.cellText()!;
		expect( text( 'dayStart', rows[0] ) ).toBe( '06:00' );
		expect( text( 'dayStart', rows[1] ) ).toBe( '00:00' );
		expect( text( 'name', rows[0] ) ).toBeUndefined();//the row's own value
	} );
	it( 'says so where the application server has no calendars table', async ()=>{
		const { fixture, list } = await create( async ()=>{ throw new Error( "Could not find type 'Calendar'." ); } );
		expect( list() ).toBeUndefined();
		expect( (<HTMLElement>fixture.nativeElement).querySelector( '[role=alert]' )?.textContent ).toContain( "Could not find type 'Calendar'." );
	} );
} );