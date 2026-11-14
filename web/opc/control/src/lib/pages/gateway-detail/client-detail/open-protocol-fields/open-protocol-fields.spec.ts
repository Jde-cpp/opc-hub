import { TestBed } from '@angular/core/testing';
import { ServerCnnctn, ServerCnnctnProps } from '../../../../model/server-cnnctn';
import { OpenProtocolFields } from './open-protocol-fields';

//docs/open-protocol/spec.md *Web* (#475):  the Open Protocol form's own fields - host, port, time zone and, behind a disclosure, the
//revision ceilings - each writing the input's string onto the record as the Properties form's do, the record composing the row's
//url and `driver` from them and saying under a field what the gateway would refuse.
const row = ( overrides:Partial<ServerCnnctnProps>={} )=>new ServerCnnctn( {slug: "pf", name: "PF", kind: "open-protocol", url: "open-protocol://plc:4545", certificateUri: "", server: undefined as any, ...overrides} as ServerCnnctnProps );
const render = ( record:ServerCnnctn )=>{
	TestBed.configureTestingModule({});
	const fixture = TestBed.createComponent( OpenProtocolFields );
	fixture.componentRef.setInput( 'record', record );
	fixture.detectChanges();
	const root = fixture.nativeElement as HTMLElement;
	const input = ( label:string )=>Array.from( root.querySelectorAll('mat-form-field') ).find( f=>f.querySelector('mat-label')!.textContent!.trim()==label )!.querySelector( 'input' ) as HTMLInputElement;
	const type = ( label:string, value:string )=>{ const control = input( label ); control.value = value; control.dispatchEvent( new Event('input', {bubbles: true}) ); fixture.detectChanges(); };
	const hint = ( label:string )=>Array.from( root.querySelectorAll('mat-form-field') ).find( f=>f.querySelector('mat-label')!.textContent!.trim()==label )!.querySelector( 'mat-hint' )!.textContent!.trim();
	return { fixture, root, input, type, hint, record: ()=>fixture.componentInstance.record() as ServerCnnctn };
};

describe( 'OpenProtocolFields', ()=>{
	it( 'shows the saved url as its host and port', ()=>{
		const { input } = render( row({url: "open-protocol://plc:4546"}) );
		expect( input("Host").value ).toBe( "plc" );
		expect( input("Port").value ).toBe( "4546" );
		expect( input("Host").required ).toBe( true );
		expect( input("Port").required ).toBe( false );
	} );
	it( 'composes open-protocol://host:port from what is typed, 4545 where the port is cleared', ()=>{
		const { type, record } = render( row() );
		type( "Host", " station-2 " );
		type( "Port", "4547" );
		expect( record().effectiveUrl ).toBe( "open-protocol://station-2:4547" );
		type( "Port", "" );
		expect( record().effectiveUrl ).toBe( "open-protocol://station-2:4545" );
		expect( record() ).toBeInstanceOf( ServerCnnctn );//a new record each edit, so the page's dirty check re-runs
	} );
	it( 'composes the driver settings from the zone and the ceilings, and none where both are empty', ()=>{
		const { type, record } = render( row() );
		expect( record().effectiveDriver ).toBeNull();
		type( "Time Zone", "Europe/Stockholm" );
		expect( record().effectiveDriver ).toBe( '{"timeZone":"Europe/Stockholm"}' );
		type( "Revision Ceilings", "0061:3 0001:6" );
		expect( record().effectiveDriver ).toBe( '{"timeZone":"Europe/Stockholm","revisions":{"0001":6,"0061":3}}' );
		type( "Time Zone", "" );
		type( "Revision Ceilings", " " );
		expect( record().effectiveDriver ).toBeNull();
	} );
	it( 'says what the gateway would refuse under the field, and the record will not save until it is right', ()=>{
		const { type, hint, record } = render( row() );
		expect( record().canSave ).toBe( true );
		type( "Host", "plc 1" );
		expect( hint("Host") ).toContain( "no space" );
		expect( record().canSave ).toBe( false );
		type( "Host", "plc" );
		type( "Port", "70000" );
		expect( hint("Port") ).toContain( "1 to 65535" );
		expect( record().canSave ).toBe( false );
		type( "Port", "4545" );
		type( "Time Zone", "Mars/Olympus" );
		expect( hint("Time Zone") ).toContain( "Not a zone" );
		expect( record().canSave ).toBe( false );
		type( "Time Zone", "UTC" );
		type( "Revision Ceilings", "61:3" );
		expect( hint("Revision Ceilings") ).toContain( "MID:revision" );
		expect( record().canSave ).toBe( false );
		type( "Revision Ceilings", "0061:3" );
		expect( record().canSave ).toBe( true );
		expect( hint("Port") ).toContain( "4545" );//the hint again, once the error has gone
	} );
	it( 'will not save with no host, and says nothing of it beyond the required marker', ()=>{
		const { type, hint, record } = render( row() );
		type( "Host", "" );
		expect( record().canSave ).toBe( false );
		expect( hint("Host") ).not.toContain( "no space" );
	} );
	it( 'keeps the ceilings behind a disclosure, closed for a row with none and open for one with some', ()=>{
		const closed = render( row() ).root.querySelector( 'details' ) as HTMLDetailsElement;
		expect( closed.open ).toBe( false );
		expect( closed.querySelector('summary')!.textContent!.trim() ).toBe( "Revision ceilings" );
		TestBed.resetTestingModule();//this test renders twice
		const open = render( row({driver: '{"revisions":{"0061":3}}'}) );
		expect( (open.root.querySelector('details') as HTMLDetailsElement).open ).toBe( true );
		expect( open.input("Revision Ceilings").value ).toBe( "0061:3" );
	} );
	it( 'suggests the zones the browser names once something is typed, by the typed text', ()=>{
		const { fixture, type } = render( row() );
		expect( fixture.componentInstance.zoneOptions() ).toEqual( [] );
		type( "Time Zone", "stockh" );
		const options = fixture.componentInstance.zoneOptions();
		expect( options ).toContain( "Europe/Stockholm" );
		expect( options.length ).toBeLessThanOrEqual( 50 );
	} );
} );