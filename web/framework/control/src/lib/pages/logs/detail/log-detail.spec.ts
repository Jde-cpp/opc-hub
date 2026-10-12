if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { vi } from 'vitest';
import { ProfileStore } from 'jde-spa';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { IGraphQL } from '../../../services/graphql';
import { LogSettings } from '../log-settings';
import { LogDetail } from './log-detail';
import { Entry } from '../log-entry';

//review3 C3: the vestigial subscription machinery is gone - subscribe()/unsubscribe(), currentSubscription, the buffer,
//onStrings, applications, connected, paused, start.  `subscription` was never assigned, so unsubscribe() was a permanent
//no-op and every one of its callers was reachable only from commented-out markup.  These pin what SURVIVED.
describe( 'LogDetail after the subscription cull', ()=>{
	let ql:any;
	let save:any;
	let exception:any;

	const entries = ( count:number )=>({ entries: Array.from({length:count}, (_,i)=>({templateId:1, argIds:[], level:3, tags:[], line:i, time:new Date().toISOString(), userId:0, fileId:0, functionId:0})), strings: [] });

	const create = ()=>{
		save = vi.fn().mockResolvedValue( undefined );
		exception = vi.fn();
		ql = { ql: vi.fn().mockResolvedValue({logs: entries(2)}) };
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: SnackbarService, useValue: {exception, warn: vi.fn()} },
			{ provide: ProfileStore, useValue: {
				load: async ( _key:string, dflt:any )=>dflt,
				loadClassArray: async ()=>[],
				save
			} }
		]});
		const fixture = TestBed.createComponent( LogDetail );
		fixture.componentRef.setInput( 'service', <IGraphQL>ql );
		return fixture.componentInstance;
	};

	it( 'loads its first page', async ()=>{
		const page = create();
		await page.ngOnInit();
		expect( ql.ql ).toHaveBeenCalledTimes( 1 );
		expect( page.isLoading() ).toBe( false );
		expect( page.data.allEntries.length ).toBe( 2 );
	} );

	it( 'reports a failed load instead of leaving the spinner up forever', async ()=>{
		const page = create();
		ql.ql.mockRejectedValue( new Error("gateway down") );
		await page.ngOnInit();
		expect( exception ).toHaveBeenCalledWith( "Could not load log entries.", expect.any(Error) );
	} );

	it( 're-reads from the top on refresh', async ()=>{
		const page = create();
		await page.ngOnInit();
		await page.refresh();
		expect( ql.ql ).toHaveBeenCalledTimes( 2 );
	} );

	//ngOnDestroy used to call unsubscribe() first; only the profile save was ever doing anything.
	it( 'still saves the log profile on the way out', ()=>{
		const page = create();
		page.profile = new LogSettings( new LogSettings() );
		page.ngOnDestroy();
		expect( save ).toHaveBeenCalledWith( "logs", page.profile );
	} );

	it( 'no longer offers the subscription surface', ()=>{
		const page:any = create();
		for( const gone of ['subscribe', 'unsubscribe', 'onStrings', 'onChangeApplication', 'onLevelChange', 'startChange'] )
			expect( gone in page ).toBe( false );
	} );
} );

//reviews-issue/231-paging.md #6 and #9:  the table and the pager both follow the data source's startIndex.  A step past what
//is loaded used to read on and then show the page at the READ offset, under a pager still reading the old range;  a sort or
//view change cleared the rows but left the pager's range behind;  and with no total bound, Next never stopped.
describe( 'LogDetail paging', ()=>{
	let ql:any;
	let serve:( q:{vars:{limit:number, skip:number}} )=>unknown;
	const tick = ()=>new Promise( r=>setTimeout(r, 5) );//past the reads' promise chains, and the table's connect(), which pages to 0 on a timer
	const id = '00000000000000000000000000000001';
	const log = ( count:number )=>Array.from( {length: count}, (_,i)=>({templateId:id, argIds:[], level:'Information', tags:[], line:i, time:new Date(Date.UTC(2026, 0, 1)-i*1000).toISOString(), userId:0, fileId:id, functionId:id}) );//the wire shape, newest first as the default view sorts

	const create = async ( available:any[] )=>{
		serve = q=>({logs: {entries: available.slice(q.vars.skip, q.vars.skip+q.vars.limit).map( e=>({...e}) ), strings: []}});//copies:  LogEntries converts them in place
		ql = { ql: vi.fn( async ( q:{vars:{limit:number, skip:number}} )=>serve(q) ) };
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: SnackbarService, useValue: {exception: vi.fn(), warn: vi.fn()} },
			{ provide: ProfileStore, useValue: { load: async ( _key:string, dflt:any )=>dflt, loadClassArray: async ()=>[], save: vi.fn().mockResolvedValue(undefined) } }
		]});
		const fixture = TestBed.createComponent( LogDetail );
		fixture.componentRef.setInput( 'service', <IGraphQL>ql );
		fixture.detectChanges();//ngOnInit
		await tick();
		fixture.detectChanges();//the table and the pager
		await tick();
		return fixture;
	};
	const shown = ( page:LogDetail )=>( (page.data as any).subject.value as Entry[] ).map( e=>e.line );//what the table was last handed
	const range = ( fixture:ComponentFixture<LogDetail> )=>{
		fixture.detectChanges();
		return ( fixture.nativeElement.querySelector('.mat-mdc-paginator-range-label') as HTMLElement ).textContent!.trim();
	};

	it( 'reads on past what is loaded and shows the page the pager is on', async ()=>{
		const fixture = await create( log(100) );
		const page = fixture.componentInstance;
		expect( page.data.allEntries ).toHaveLength( 63 );//three pages a read
		page.onPagerChange( {startIndex: 43, pageSize: 21} );//the row step that crosses the end of what is loaded
		await tick();
		expect( ql.ql.mock.calls[1][0].vars.skip ).toBe( 63 );
		expect( shown(page) ).toEqual( Array.from({length: 21}, (_,i)=>43+i) );
		expect( range(fixture) ).toBe( '44 – 64 of 100' );
	} );

	//reviews-issue/231-paging.md #7:  the page used to be shown short - empty, on a page turn - for the whole round trip
	it( 'keeps the rows on screen until the read lands', async ()=>{
		const fixture = await create( log(100) );
		const page = fixture.componentInstance;
		let land!:()=>void;
		ql.ql.mockImplementationOnce( ( q:any )=>new Promise( r=>land = ()=>r(serve(q)) ) );
		page.onPagerChange( {startIndex: 63, pageSize: 21} );
		expect( shown(page) ).toEqual( Array.from({length: 21}, (_,i)=>i) );
		expect( range(fixture) ).toBe( '64 – 84' );
		land();
		await tick();
		expect( shown(page) ).toEqual( Array.from({length: 21}, (_,i)=>63+i) );
	} );

	it( 'knows the total once a read comes back short, and reads no further', async ()=>{
		const fixture = await create( log(30) );
		const page = fixture.componentInstance;
		expect( page.data.length() ).toBe( 30 );
		page.onPagerChange( {startIndex: 21, pageSize: 21} );
		expect( ql.ql ).toHaveBeenCalledTimes( 1 );
		expect( range(fixture) ).toBe( '22 – 30 of 30' );
		expect( fixture.nativeElement.querySelector('button[aria-label="Next page"]').disabled ).toBe( true );
	} );

	it( 'puts the pager back on the first page on a sort or a view change', async ()=>{
		const fixture = await create( log(100) );
		const page = fixture.componentInstance;
		page.onPagerChange( {startIndex: 21, pageSize: 21} );
		expect( range(fixture) ).toBe( '22 – 42' );
		page.onSort( {active: 'level', direction: 'asc'} );
		await tick();
		expect( range(fixture) ).toBe( '1 – 21' );
		page.onPagerChange( {startIndex: 21, pageSize: 21} );
		page.onViewChange( 0 );
		await tick();
		expect( range(fixture) ).toBe( '1 – 21' );
	} );
} );