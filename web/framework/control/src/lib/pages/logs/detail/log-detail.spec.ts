import { ComponentFixture, TestBed } from '@angular/core/testing';
import { ProfileStore } from 'jde-spa';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { IGraphQL } from '../../../services/graphql';
import { LogSettings } from '../log-settings';
import { LogDetail } from './log-detail';
import { Entry } from '../log-entry';
import { flush, profileStoreStub } from 'jde-spa/testing';
import { logEntry, snackbarStub } from 'jde-framework/testing';
import { Guid } from '../../../model/guid';

//review3 C3: the vestigial subscription machinery is gone - subscribe()/unsubscribe(), currentSubscription, the buffer,
//onStrings, applications, connected, paused, start.  `subscription` was never assigned, so unsubscribe() was a permanent
//no-op and every one of its callers was reachable only from commented-out markup.  These pin what SURVIVED.
describe( 'LogDetail after the subscription cull', ()=>{
	const create = ()=>{
		const store = profileStoreStub();
		const snackbar = snackbarStub();
		const ql = { ql: vi.fn().mockResolvedValue({logs: {entries: [logEntry(0), logEntry(1)], strings: []}}) };
		TestBed.configureTestingModule({ providers: [
			{ provide: SnackbarService, useValue: snackbar },
			{ provide: ProfileStore, useValue: store }
		]});
		const fixture = TestBed.createComponent( LogDetail );
		fixture.componentRef.setInput( 'service', <IGraphQL><unknown>ql );
		return { page: fixture.componentInstance, ql, store, snackbar };
	};

	it( 'loads its first page', async ()=>{
		const { page, ql } = create();
		await page.ngOnInit();
		expect( ql.ql ).toHaveBeenCalledTimes( 1 );
		expect( page.isLoading() ).toBe( false );
		expect( page.data.allEntries.length ).toBe( 2 );
	} );

	it( 'reports a failed load instead of leaving the spinner up forever', async ()=>{
		const { page, ql, snackbar } = create();
		ql.ql.mockRejectedValue( new Error("gateway down") );
		await page.ngOnInit();
		expect( snackbar.exception ).toHaveBeenCalledWith( "Could not load log entries.", expect.any(Error) );
	} );

	it( 're-reads from the top on refresh', async ()=>{
		const { page, ql } = create();
		await page.ngOnInit();
		await page.refresh();
		expect( ql.ql ).toHaveBeenCalledTimes( 2 );
	} );

	//ngOnDestroy used to call unsubscribe() first; only the profile save was ever doing anything.
	it( 'still saves the log profile on the way out', ()=>{
		const { page, store } = create();
		page.profile = new LogSettings( new LogSettings() );
		page.ngOnDestroy();
		expect( store.save ).toHaveBeenCalledWith( "logs", page.profile );
	} );

	it( 'no longer offers the subscription surface', ()=>{
		const page:any = create().page;
		for( const gone of ['subscribe', 'unsubscribe', 'onStrings', 'onChangeApplication', 'onLevelChange', 'startChange'] )
			expect( gone in page ).toBe( false );
	} );
} );

//reviews-issue/231-paging.md #6 and #9:  the table and the pager both follow the data source's startIndex.  A step past what
//is loaded used to read on and then show the page at the READ offset, under a pager still reading the old range;  a sort or
//view change cleared the rows but left the pager's range behind;  and with no total bound, Next never stopped.
describe( 'LogDetail paging', ()=>{
	const tick = ()=>flush( 2 );//past the reads' promise chains, and the table's connect(), which pages to 0 on a timer
	const log = ( count:number )=>Array.from( {length: count}, (_, i)=>logEntry(i) );

	const create = async ( available:any[] )=>{
		const serve = ( q:{vars:{limit:number, skip:number}} )=>({logs: {entries: available.slice(q.vars.skip, q.vars.skip+q.vars.limit).map( e=>({...e}) ), strings: []}});//copies:  LogEntries converts them in place
		const ql = { ql: vi.fn( async ( q:{vars:{limit:number, skip:number}} )=>serve(q) ) };
		TestBed.configureTestingModule({ providers: [
			{ provide: SnackbarService, useValue: snackbarStub() },
			{ provide: ProfileStore, useValue: profileStoreStub() }
		]});
		const fixture = TestBed.createComponent( LogDetail );
		fixture.componentRef.setInput( 'service', <IGraphQL><unknown>ql );
		fixture.detectChanges();//ngOnInit
		await tick();
		fixture.detectChanges();//the table and the pager
		await tick();
		return { fixture, page: fixture.componentInstance, ql, serve };
	};
	const shown = ( page:LogDetail )=>( (page.data as any).subject.value as Entry[] ).map( e=>e.line );//what the table was last handed
	const range = ( fixture:ComponentFixture<LogDetail> )=>{
		fixture.detectChanges();
		return ( fixture.nativeElement.querySelector('.mat-mdc-paginator-range-label') as HTMLElement ).textContent!.trim();
	};

	it( 'reads on past what is loaded and shows the page the pager is on', async ()=>{
		const { fixture, page, ql } = await create( log(100) );
		expect( page.data.allEntries ).toHaveLength( 63 );//three pages a read
		page.onPagerChange( {startIndex: 43, pageSize: 21} );//the row step that crosses the end of what is loaded
		await tick();
		expect( ql.ql.mock.calls[1][0].vars.skip ).toBe( 63 );
		expect( shown(page) ).toEqual( Array.from({length: 21}, (_,i)=>43+i) );
		expect( range(fixture) ).toBe( '44 – 64 of 100' );
	} );

	//reviews-issue/231-paging.md #7:  the page used to be shown short - empty, on a page turn - for the whole round trip
	it( 'keeps the rows on screen until the read lands', async ()=>{
		const { fixture, page, ql, serve } = await create( log(100) );
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
		const { fixture, page, ql } = await create( log(30) );
		expect( page.data.length() ).toBe( 30 );
		page.onPagerChange( {startIndex: 21, pageSize: 21} );
		expect( ql.ql ).toHaveBeenCalledTimes( 1 );
		expect( range(fixture) ).toBe( '22 – 30 of 30' );
		expect( fixture.nativeElement.querySelector('button[aria-label="Next page"]').disabled ).toBe( true );
	} );

	it( 'puts the pager back on the first page on a sort or a view change', async ()=>{
		const { fixture, page } = await create( log(100) );
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

//reviews/angular-refactor.md B7:  the hide, next and show-hidden buttons the old toolbar parked, against the row clicked.  Lines
//alternate between two messages unless a test says otherwise.
describe( 'LogDetail message actions', ()=>{
	const other = '00000000000000000000000000000002';
	const tick = ()=>flush( 2 );
	const alternating = ( count:number )=>Array.from( {length: count}, (_, i)=>logEntry(i, i%2 ? {templateId: other} : {}) );

	const create = async ( available:any[], profile?:object )=>{
		const ql = { ql: vi.fn( async ( q:{vars:{limit:number, skip:number}} )=>({logs: {entries: available.slice(q.vars.skip, q.vars.skip+q.vars.limit).map( e=>({...e}) ), strings: []}}) ) };
		const snackbar = snackbarStub();
		TestBed.configureTestingModule({ providers: [
			{ provide: SnackbarService, useValue: snackbar },
			{ provide: ProfileStore, useValue: profileStoreStub( profile ? {logs: profile} : {} ) }
		]});
		const fixture = TestBed.createComponent( LogDetail );
		fixture.componentRef.setInput( 'service', <IGraphQL><unknown>ql );
		fixture.detectChanges();
		await tick();
		fixture.detectChanges();
		await tick();
		fixture.detectChanges();
		return { fixture, page: fixture.componentInstance, ql, snackbar };
	};
	const shown = ( page:LogDetail )=>( (page.data as any).subject.value as Entry[] ).map( e=>e.line );
	const selected = ( page:LogDetail )=>page.data.allEntries.filter( e=>e.selected ).map( e=>e.line );
	const button = ( fixture:ComponentFixture<LogDetail>, label:string )=>{
		fixture.detectChanges();
		return fixture.nativeElement.querySelector( `button[aria-label="${label}"]` ) as HTMLButtonElement;
	};
	const row = ( fixture:ComponentFixture<LogDetail>, index:number )=>fixture.nativeElement.querySelectorAll( 'mat-row' )[index] as HTMLElement;

	it( 'hides every row of the selected message, remembers it, and shows them again', async ()=>{
		const { fixture, page } = await create( alternating(10) );
		expect( button(fixture, 'Hide this message').disabled ).toBe( true );
		expect( button(fixture, 'Show hidden messages').disabled ).toBe( true );
		row( fixture, 1 ).click();
		expect( button(fixture, 'Hide this message').disabled ).toBe( false );
		button( fixture, 'Hide this message' ).click();
		expect( shown(page) ).toEqual( [0, 2, 4, 6, 8] );
		expect( page.profile.hiddenMessages.map( g=>g.equals(new Guid(other)) ) ).toEqual( [true] );
		expect( button(fixture, 'Hide this message').disabled ).toBe( true );//the selected row went with its message
		button( fixture, 'Show hidden messages' ).click();
		expect( shown(page) ).toEqual( [0, 1, 2, 3, 4, 5, 6, 7, 8, 9] );
		expect( button(fixture, 'Show hidden messages').disabled ).toBe( true );
	} );

	it( 'keeps the messages hidden last time hidden', async ()=>{
		const { fixture } = await create( alternating(10), JSON.parse(JSON.stringify( {hiddenMessages: [new Guid(other)]} )) );
		expect( shown(fixture.componentInstance) ).toEqual( [0, 2, 4, 6, 8] );
		expect( button(fixture, 'Show hidden messages').disabled ).toBe( false );
	} );

	it( 'steps to the next row of the message, reading on past what is loaded', async ()=>{
		const { fixture, page, ql, snackbar } = await create( Array.from( {length: 100}, (_, i)=>logEntry(i, i==1 || i==80 ? {templateId: other} : {}) ) );
		expect( page.data.allEntries ).toHaveLength( 63 );
		row( fixture, 1 ).dispatchEvent( new KeyboardEvent('keydown', {key: 'Enter'}) );
		expect( selected(page) ).toEqual( [1] );
		await page.navigateNext();
		expect( ql.ql.mock.calls[1][0].vars.skip ).toBe( 63 );
		expect( selected(page) ).toEqual( [80] );
		expect( shown(page)[0] ).toBe( 63 );//the page it is on, 21 a page
		expect( snackbar.warn ).not.toHaveBeenCalled();
	} );

	it( 'says so when no later row has the message, and reads no further', async ()=>{
		const { fixture, page, ql, snackbar } = await create( alternating(10) );
		row( fixture, 9 ).click();
		await page.navigateNext();
		expect( snackbar.warn ).toHaveBeenCalledWith( "No more rows of this message." );
		expect( selected(page) ).toEqual( [9] );
		expect( ql.ql ).toHaveBeenCalledTimes( 1 );
	} );
} );